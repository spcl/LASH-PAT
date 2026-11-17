#include "left_right_routing.hpp"
#include "routing_tables.hpp"
#include "routing_utils.hpp"
#include "../config_utils.hpp"
#include "topology_hash.hpp"
#include <filesystem>
#include <vector>
#include <tuple>
#include <map>
#include <set>
#include <climits>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstdlib>

using namespace std;

// Constructor
left_right_routing::left_right_routing(const Configuration config) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_is_initialized = false;
}

vector<tuple<int,int,int>> left_right_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this topology and configuration is already stored in the cache
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "left_right_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType2 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			cout << "- Loaded Left/Right routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<left_right_routing*>(this)->initialize(router_list, n_routers);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType2 rt;
				rt._routing_table = _routing_table;
				rt.save(rt_cache_file);
				cout << "Saved Left/Right routing tables to cache: " << rt_cache_file << endl;
			}
		}
		_is_initialized = true;
	}
	vector<tuple<int,int,int>> valid_next_hops;
	int output_port;
	// If we are already at the destination router, we can directly route to the destination node
	if (cur_rid == dst_rid) {	
		output_port = get<0>(router_list[0][cur_rid][dst_nid]);						// Identify output port to dst_nid
		valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs-1));				// All VCs are valid for ejection
	} else {
		// If we are at the source (injection), we replace the real input-port with -1 to indicate that we are injecting.
		if (cur_rid == src_rid) {
			in_port = -1;	// Use -1 to indicate injection
		}
		// Read next hop from routing table. Format is (output_port, output_vc, path_length, next_rid)
		vector<int> next_hops = _routing_table[cur_rid][dst_rid][in_port];
		for (int next_hop : next_hops) {
			output_port = next_hop;
			valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs-1));			// All VCs are valid for the next hop (no VC restrictions in this routing algorithm)
		}
	}
	return valid_next_hops;
}

void left_right_routing::initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers){
	// Compute the width of each router in the left/right hierarchy.
	compute_router_widths(router_list, n_routers);
	// Compute valid next hops for all source-destination pairs based on the computed router widths
	for (int src_rid = 0; src_rid < n_routers; ++src_rid) {
		find_turn_based_paths_from_source(router_list, n_routers, src_rid, _routing_table, [this](int a,int b,int c){ return is_legal_turn(a,b,c);});
	}
	// Mark the routing table as initialized
	_is_initialized = true;
}

void left_right_routing::compute_router_widths(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers) const {
	// Randomly select a root router
	int root_rid = rand() % n_routers;
	// Step 1: Perform a breadth-first search (BFS) from the root router to get the BFS Tree
	std::vector<bool> visited(n_routers, false);
	std::vector<int> parent(n_routers, -1);
	std::vector<std::vector<int>> children(n_routers);
	std::vector<int> queue;
	visited[root_rid] = true;
	queue.push_back(root_rid);
	int cur_rid, nei_rid;
	while (!queue.empty()) {
		cur_rid = queue.front();
		queue.erase(queue.begin());
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
			nei_rid = it->first;
			if (!visited[nei_rid]) {
				visited[nei_rid] = true;
				parent[nei_rid] = cur_rid;
				children[cur_rid].push_back(nei_rid);
				queue.push_back(nei_rid);
			}
		}
	}
	// Step 2: Run DFS pre-order on the BFS Tree (not on the network graph) to assign widths to routers.
	int counter = 0;
	std::function<void(int)> dfs_preorder = [&](int rid) {
		_router_widths[rid] = counter++;
		for (int child_rid : children[rid]) {
			dfs_preorder(child_rid);
		}
	};
	dfs_preorder(root_rid);
}

bool left_right_routing::is_legal_turn(int prv_rid, int cur_rid, int nxt_rid) const {
	// If we are at injection (prev_rid == -1), we can take any turn
	if (prv_rid == -1) {
		return true;
	}
	// Definition of left direction as in Fig. 3 in the paper (not as described in text in paragraph 3.2c) which is likely the wrong way around.
	bool first_edge_is_left = (_router_widths[prv_rid] > _router_widths[cur_rid]);
	bool second_edge_is_left = (_router_widths[cur_rid] > _router_widths[nxt_rid]);
	// The only illegal turn is right->left
	return !(first_edge_is_left == false && second_edge_is_left == true);
}

// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> left_right_routing::get_max_routing_table_size(int N, int P, int V) {
	return get_max_size_of_turn_based_routing_table(_routing_table, N, P, V);
}
