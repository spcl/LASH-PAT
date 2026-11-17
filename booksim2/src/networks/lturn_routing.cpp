#include "lturn_routing.hpp"
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
lturn_routing::lturn_routing(const Configuration config) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_is_initialized = false;
}

vector<tuple<int,int,int>> lturn_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this topology and configuration is already stored in the cache
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "lturn_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType2 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			cout << "- Loaded Up/Down routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<lturn_routing*>(this)->initialize(router_list, n_routers);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType2 rt;
				rt._routing_table = _routing_table;
				rt.save(rt_cache_file);
				cout << "Saved Up/Down routing tables to cache: " << rt_cache_file << endl;
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

void lturn_routing::initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers){
	// Compute the level of each router in the up/down hierarchy and the widths of the routers in the left/right hierarchy
	int root_rid = compute_router_levels_and_widths(router_list, n_routers);
	compute_explicitly_prohibited_turns(router_list, n_routers, root_rid);
	// Compute valid next hops for all source-destination pairs based on the computed router widths
	for (int src_rid = 0; src_rid < n_routers; ++src_rid) {
		find_turn_based_paths_from_source(router_list, n_routers, src_rid, _routing_table, [this](int a,int b,int c){ return is_legal_turn(a,b,c);});
	}
	// Mark the routing table as initialized
	_is_initialized = true;
}

int lturn_routing::compute_router_levels_and_widths(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers) const {
	// Randomly select a root router
	int root_rid = rand() % n_routers;
	// Step 1: Perform a breadth-first search (BFS) from the root router to get the BFS Tree
	std::vector<bool> visited(n_routers, false);
	std::vector<int> parent(n_routers, -1);
	std::vector<std::vector<int>> children(n_routers);
	std::vector<int> queue;
	visited[root_rid] = true;
	queue.push_back(root_rid);
	_router_levels[root_rid] = 0;
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
				_router_levels[nei_rid] = _router_levels[cur_rid] + 1;
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
	// Return the root router ID for future use
	return root_rid;
}

lturn_routing::ChannelType lturn_routing::get_channel_type(int from_rid, int to_rid) const {
	// From the paper: <<...if two connected nodes are at the same depth, the one at the right side is assumed to be closer to the root node in vertical direction.>>
	bool is_left = _router_widths[from_rid] > _router_widths[to_rid];
	bool is_up = (_router_levels[from_rid] > _router_levels[to_rid]) || ((_router_levels[from_rid] == _router_levels[to_rid]) && (_router_widths[from_rid] < _router_widths[to_rid]));
	if (is_left && is_up) {
		return ChannelType::LU;
	} else if (is_left && !is_up) {
		return ChannelType::LD;
	} else if (!is_left && is_up) {
		return ChannelType::RU;
	} else { // (!is_left && !is_up)
		return ChannelType::RD;
	}
}

bool lturn_routing::is_legal_turn(int prv_rid, int cur_rid, int nxt_rid) const {
	// We do not allow for 180 degree turns
	if (prv_rid == nxt_rid) {
		return false;
	}
	// If we are at injection (prev_rid == -1), we can take any turn
	if (prv_rid == -1) {
		return true;
	}
	// Identify the channel types of the incoming and outgoing channels
	ChannelType ct1 = get_channel_type(prv_rid, cur_rid);
	ChannelType ct2 = get_channel_type(cur_rid, nxt_rid);
	// Rule 1: No LU channels after using any channel except for LU
	if (ct1 != ChannelType::LU && ct2 == ChannelType::LU) {
		return false;
	}
	// Rule 2: Avoid specific LD->RU and LD->RD turns that have been detected in advance.
	if (_explicitly_prohibited_turns.find(make_tuple(prv_rid, cur_rid, nxt_rid)) != _explicitly_prohibited_turns.end()) {
		return false;
	}
	// If none of the above rules are violated, the turn is legal
	return true;
}

void lturn_routing::compute_explicitly_prohibited_turns(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int root_rid) const {
	// Variables used throughout the function
	int cur_rid, nei_rid;							// Router IDs
	int ru_cnt, rd_cnt;								// Counters for RU and RD channels
	// Step 1: Select routers based on condition that is explained in the paper.
	vector<int> selected_rids;
	for (cur_rid = 0; cur_rid < n_routers; ++cur_rid) {
		ru_cnt = 0;
		rd_cnt = 0;
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
			nei_rid = it->first;
			ChannelType ct = get_channel_type(cur_rid, nei_rid);
			if (ct == ChannelType::RU) {
				ru_cnt++;
			} else if (ct == ChannelType::RD) {
				rd_cnt++;
			}
		}
		if ((ru_cnt >= 2) || ((ru_cnt >= 1) && (rd_cnt >= 1))){
			selected_rids.push_back(cur_rid);
		}
	}
	// Step 2: Compute DFS order and sort selected routers accordingly
	vector<int> dfs_order(n_routers, -1);
	int counter = 0;
	std::function<void(int)> dfs = [&](int rid) {
		dfs_order[rid] = counter++;
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][rid].begin(); it != router_list[1][rid].end(); ++it) {
			nei_rid = it->first;
			if (dfs_order[nei_rid] == -1) {
				dfs(nei_rid);	
			}
		}
	};
	dfs(root_rid); // From the paper, it is not fully clear where to start the DFS. We start from the MST root used to compute router levels and widths.
	sort(selected_rids.begin(), selected_rids.end(), [&](int a, int b) { return dfs_order[a] < dfs_order[b];});
	set<pair<int,int>> visited;	// Used to keep track of visited channels during DFS. Key is (from_rid, to_rid)
	// Step 3: Process the selected routers in the computed order and identify prohibited turns based on the rules explained in the paper.
	for (int sel_rid : selected_rids) {
		// Iterate over neighbors that are reachable via RU or RD channels
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][sel_rid].begin(); it != router_list[1][sel_rid].end(); ++it) {
			nei_rid = it->first;
			ChannelType ct = get_channel_type(sel_rid, nei_rid);
			if (ct == ChannelType::RU || ct == ChannelType::RD) {
				// Perform a DFS going over this channel and respecting existing turn restrictions. 
				// Forbid each turn at the current router that enters via a LD channel
				visited.clear();
				std::function<void(int,int)> dfs = [&](int prv_rid, int cur_rid) {
					visited.insert(make_pair(prv_rid, cur_rid));
					for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
						int nxt_rid = it->first;
						// Do not use 180 degree turns.
						if (nxt_rid == prv_rid) {
							continue;
						}
						// If this is a left-up channel, we skip
						if (get_channel_type(cur_rid, nxt_rid) == ChannelType::LU) {
							continue;
						}
						// If this channel is already visited, we skip it (we assume that this was meant with "marked" in the paper)
						if (visited.find(make_pair(cur_rid, nxt_rid)) != visited.end()) {
							continue;
						}
						// If we already prohibited this turn explicitly, we skip and go back to previous router
						if (_explicitly_prohibited_turns.find(make_tuple(prv_rid, cur_rid, nxt_rid)) != _explicitly_prohibited_turns.end()) {
							continue;
						}
						// Special rule for case 1-(b) in the paper (i.e. sel_rid -> nei_rid is a RD channel: Don't explore LD-RU turns.
						if (ct == ChannelType::RD && get_channel_type(prv_rid, cur_rid) == ChannelType::LD && get_channel_type(cur_rid, nxt_rid) == ChannelType::RU) {
							continue;
						}
						// If we reach the selected router via a LD channel, we prohibit the turn and go back to previous router (regardless of whether this turn closes a cycle or not)
						if ((nxt_rid == sel_rid) && (get_channel_type(cur_rid, nxt_rid) == ChannelType::LD)) {
							// Sanity checks with text in the assertion
							assert(nxt_rid == sel_rid && "Error in lturn_routing::compute_explicitly_prohibited_turns: The cycle should reach the selected router.");
							assert(get_channel_type(cur_rid, sel_rid) == ChannelType::LD && "Error in lturn_routing::compute_explicitly_prohibited_turns: The cycle should reach the selected router via a LD channel.");
							// Prohibit the turn and go back to previous router
							_explicitly_prohibited_turns.insert(make_tuple(cur_rid, sel_rid, nei_rid));
							continue;
						}
						// If we already visited this channel we skip it
						if (visited.find(make_pair(cur_rid, nxt_rid)) != visited.end()) {
							continue;
						} 
						// If none of the above conditions hold, we can explore this channel and continue DFS
						dfs(cur_rid, nxt_rid);
					}
				};
				dfs(sel_rid, nei_rid);
			}
		}
	}
}



// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> lturn_routing::get_max_routing_table_size(int N, int P, int V) {
	return get_max_size_of_turn_based_routing_table(_routing_table, N, P, V);
}
