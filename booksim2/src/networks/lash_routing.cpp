#include "lash_routing.hpp"
#include "routing_tables.hpp"
#include "topology_hash.hpp"
#include "../config_utils.hpp"
#include "edge_based_cdg.hpp"
#include "routing_function.hpp"
#include <queue>
#include <vector>
#include <iostream>
#include <algorithm>
#include <memory>
#include <tuple>
#include <vector>
#include <map>
#include <set>
#include <climits>
#include <cmath>
#include <filesystem>

using namespace std;

// Constructor
lash_routing::lash_routing(const Configuration config, vector<int> rouers_with_nodes) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_is_initialized = false;
	_routers_with_nodes = rouers_with_nodes;
}

// Initialization which is only called once per simulation and which computes all routing tables.
void lash_routing::initialize(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs) {
	cout << "Initializing LASH routing..." << endl;
	// Compute all routing tables here
	for (int dst_rid : _routers_with_nodes) {
		find_and_commit_paths(router_list, n_routers, n_vcs, dst_rid);
	}
	// If we need more layers than we have VCs available, we cannot route all paths and need to exit with an error.
	if (_n_layers > n_vcs) {
		cout << "Error in lash_routing::initialize: The number of layers needed to route all paths (" << _n_layers << ") exceeds the number of available VCs (" << n_vcs << ")" << endl;
        exit(4);	// Return code 4 is "Insufficient VCs"
	} else {
		cout << "Successfully initialized LASH routing with " << _n_layers << " layers (" << n_vcs << " VCs available)." << endl;
		// Configure the VC map to map the layer to the corresponding VCs.
		// If there are more VCs than we need layers, we assign the remaining VCs to layers in a round-robin manner.
		for (int vc = 0; vc < n_vcs; vc++) {
			int layer = vc % _n_layers;
			_vc_map[layer].push_back(vc);
		}
	}
}

void lash_routing::find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_vcs, int dst_rid) {
	// Variables
	int cur_rid;
	// Use BFS to find the shortest path from all other routers to dst_rid.
	queue<int> q;
	set<int> visited;
	map<int,int> parent; // To reconstruct the path after reaching dst_rid
	q.push(dst_rid);
	visited.insert(dst_rid);
	while (!q.empty()) {
		// Dequeue the next router to explore
		int cur_rid = q.front();
		q.pop();
		// Go over all neighbors of cur_rid
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
			int nxt_rid = it->first;
			// Only visit neighbors that have not been visited yet.
			if (visited.find(nxt_rid) == visited.end()) {
				visited.insert(nxt_rid);
				parent[nxt_rid] = cur_rid;
				q.push(nxt_rid);
			}
		}
	}
	// Now, we use backtracking to find paths to the destination starting from all source routers.
	for (int src_rid : _routers_with_nodes) {
		if (src_rid != dst_rid) {
			// Add the first hop from src_rid towards dst_rid to the routing table.
			_routing_table[src_rid][dst_rid] = get<0>(router_list[1][src_rid][parent[src_rid]]);
			// Construct the path from src_rid to dst_rid for layer assignment.
			vector<int> path;
			cur_rid = src_rid;
			while (cur_rid != dst_rid) {
				// Sanity check to ensure that we have a path from src_rid to dst_rid. If not, we exit with an error since this should not happen in a connected topology.
				if (parent.find(cur_rid) == parent.end()) {
					cout << "Error in lash_routing::find_and_commit_path: No path found from src_rid " << src_rid << " to dst_rid " << dst_rid << endl;
					exit(1);
				}
				path.push_back(cur_rid);
				cur_rid = parent[cur_rid];
			}
			path.push_back(dst_rid);
			// Assign the identified path to a layer and configure the layer map accordingly.
			assign_path_to_layer(router_list, n_routers, n_vcs, src_rid, dst_rid, path);			
		}
	}
}

void lash_routing::assign_path_to_layer(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_vcs, int src_rid, int dst_rid, vector<int> path) {
	int cur_layer = 0;
	bool success = false;
	tuple<int,int,int> from, to;
	while (!success) {
		// Increase number of layers if needed
		if (cur_layer >= _n_layers) {
			_n_layers++;
			_cdgs.push_back(CDG_E(router_list, n_routers, n_vcs));
		}
		// Identify CDG edges, that we would need to insert into the CDG in order to add this path to the current layer
		vector<pair<tuple<int,int,int>,tuple<int,int,int>>> additional_cdg_edges;
		vector<pair<tuple<int,int,int>,tuple<int,int,int>>> additional_cdg_edges_added;
		for (size_t i = 1; i < path.size() - 1; i++) {
			// Use VC 0 since we have one CDG per layer
			from = make_tuple(path[i-1], path[i], 0);
			to = make_tuple(path[i], path[i+1], 0);
			if (!_cdgs[cur_layer].has_edge(from, to)) {
				additional_cdg_edges.push_back(make_pair(from, to));
			}
		}
		// Check if we can add the additional CDG edges to the current layer without creating cycles in the CDG
		success = true;
		for (pair<tuple<int,int,int>,tuple<int,int,int>> edge : additional_cdg_edges) {
			if (_cdgs[cur_layer].creates_cycle(edge.first, edge.second)) {
				success = false;
			} else {
				_cdgs[cur_layer].insert_edge(edge.first, edge.second);
				additional_cdg_edges_added.push_back(edge);
			}
		}
		// If we could not add all additional CDG edges to the current layer, we need to remove the already added CDG edges and try again with the next layer.
		// Otherwise, we have successfully added the path to the current layer and can exit the loop. In that case, we need to set the layer map accordingly.
		if (!success) {
			for (pair<tuple<int,int,int>,tuple<int,int,int>> edge : additional_cdg_edges_added) {
				_cdgs[cur_layer].remove_edge(edge.first, edge.second);
			}
			cur_layer++;
		}
	}
	_layer_map[src_rid][dst_rid] = cur_layer;
}




vector<tuple<int,int,int>> lash_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this topology and configuration is already stored in the cache.
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "lash_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + "_" + to_string(n_vcs) + "vcs" + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType4 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			_layer_map = rt._layer_map;
			_vc_map = rt._vc_map;
			_n_layers = _vc_map.size();
			cout << "- Loaded LASH routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<lash_routing*>(this)->initialize(router_list, n_routers, n_nodes, n_vcs);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType4 rt;
				rt._routing_table = _routing_table;
				rt._layer_map = _layer_map;
				rt._vc_map = _vc_map;
				rt.save(rt_cache_file);
				cout << "Saved LASH routing tables to cache: " << rt_cache_file << endl;
			}
		}
		_is_initialized = true;
	}
	vector<tuple<int,int,int>> valid_next_hops;
	int output_port;
	// If we are already at the destination router, we can directly route to the destination node
	if (cur_rid == dst_rid) {	
		output_port = get<0>(router_list[0][cur_rid][dst_nid]);						// Identify output port to dst_nid
		valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs - 1));			// We can us all VCs for ejection.
	} else {
		// Read the output port from the routing table
		output_port = _routing_table[cur_rid][dst_rid];
		// If we are injecting, we need to send the packet to the correct VC, otherwise, the VC stays as is.
		if (cur_rid == src_rid) {
			int layer = _layer_map[src_rid][dst_rid];
			// Add all available layers s.t. the selection function can select between them for load-balancing.
			for (int vc : _vc_map[layer]) {
				valid_next_hops.push_back(make_tuple(output_port, vc, vc));			// Potentially multiple available VCs for injection.
			}
		} else {
			valid_next_hops.push_back(make_tuple(output_port, in_vc, in_vc));		// Output port and same VC for forwarding
		}
	}
	return valid_next_hops;
}

// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> lash_routing::get_max_routing_table_size(int N, int P, int V) {
	int dense_rows = N;
	int bits_list = ceil(log2(P));
	int bits_bitmap = P;
	int csr_it_rows = N;
	int csr_it_bits = ceil(log2(N));
	int csr_rows = N;
	int layer_table_bits = N * ceil(log2(V));	// For each destination, we need to store the assigned layer (VC)
	int dense_list = dense_rows * bits_list + layer_table_bits;
	int dense_bitmap = dense_rows * bits_bitmap + layer_table_bits;
	int csr_list = csr_it_rows * csr_it_bits + csr_rows * bits_list + layer_table_bits;
	int csr_bitmap = csr_it_rows * csr_it_bits + csr_rows * bits_bitmap + layer_table_bits;
	return make_tuple(dense_list, dense_bitmap, csr_list, csr_bitmap);
}
