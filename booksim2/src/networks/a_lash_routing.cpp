#include "a_lash_routing.hpp"
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
#include <numeric>

using namespace std;

// Constructor
a_lash_routing::a_lash_routing(const Configuration config, vector<int>& routers_with_nodes, vector<map<int,int>>& router_and_outport_to_next_router) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_is_initialized = false;
	_routers_with_nodes = routers_with_nodes;
	_router_and_outport_to_next_router = router_and_outport_to_next_router;
}

// Initialization which is only called once per simulation and which computes all routing tables.
void a_lash_routing::initialize(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs) {
	cout << "Initializing A-LASH routing..." << endl;
	// Configure routing table and layer map data structures.
	int n_layers = find_and_commit_paths(router_list, n_routers, n_nodes, n_vcs);
	// If we need more layers than we have VCs available, we cannot route all paths and need to exit with an error.
	if (n_layers > n_vcs) {
		cout << "Error in a_lash_routing::initialize: The number of layers needed to route all paths (" << n_layers << ") exceeds the number of available VCs (" << n_vcs << ")" << endl;
        exit(4);	// Return code 4 is "Insufficient VCs"
	} else {
		cout << "Successfully initialized A-LASH routing with " << n_layers << " layers (" << n_vcs << " VCs available)." << endl;
	}
	// Try to add paths to multiple layers if possible to increase path diversity and potentially reduce congestion.
	try_to_add_paths_to_muliple_layers(n_routers, n_vcs);
}

vector<tuple<int,int,int>> a_lash_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this 
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "a_lash_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + "_" + to_string(n_vcs) + "vcs" + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType6 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			_layer_map = rt._layer_map;
			cout << "- Loaded A-LASH routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<a_lash_routing*>(this)->initialize(router_list, n_routers, n_nodes, n_vcs);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType6 rt;
				rt._routing_table = _routing_table;
				rt._layer_map = _layer_map;
				rt.save(rt_cache_file);
				cout << "Saved A-LASH routing tables to cache: " << rt_cache_file << endl;
			}
		}
		_is_initialized = true;
		cout << "Finished initializing A-LASH routing." << endl;
	}
	vector<tuple<int,int,int>> valid_next_hops;
	// If we are already at the destination router, we can directly route to the destination node
	if (cur_rid == dst_rid) {	
		int output_port = get<0>(router_list[0][cur_rid][dst_nid]);						// Identify output port to dst_nid
		valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs - 1));			// We can us all VCs for ejection.
	} else {
		// If we are not at the destination, we read the list of output ports from the routing table
		vector<int> output_ports = _routing_table[cur_rid][dst_rid];
		// Configure next hops. If at source, inspect the layer map and only use valid output ports with corresponding VCs.
		// If not at source, use all output port and use the same VC as the one we are currently on.
		for (int output_port : output_ports) {
			if (cur_rid == src_rid) {
				vector<int> output_vcs_for_port = _layer_map[cur_rid][dst_rid][output_port];
				for (int output_vc : output_vcs_for_port) {
					valid_next_hops.push_back(make_tuple(output_port, output_vc, output_vc));
				}
			} else {
				valid_next_hops.push_back(make_tuple(output_port, in_vc, in_vc));
			}
		}
	}
	return valid_next_hops;
}

int a_lash_routing::find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs) {
	// Variables
	int cur_rid, nei_rid;
	int nei_dist;
	int layer;
	int out_port;
	int max_layer_used = -1;
	// Initialize one CDG per layer
	for (int layer = 0; layer < n_vcs; layer++) {
		_cdgs.push_back(CDG_E(router_list, n_routers, 1));
	}
	// Iterate over all destination routers
	for (int dst_rid : _routers_with_nodes) {
		cout << "-- Configuring paths to destination router " << dst_rid << "..." << endl;
		// Step 1: Compute all shortest paths from any source to dst_rid using BFS
		vector<vector<int>> next(n_routers);
		vector<int> dist(n_routers, INT_MAX);
		queue<int> q;
		dist[dst_rid] = 0;
		q.push(dst_rid);
		while (!q.empty()) {
			cur_rid = q.front();
			q.pop();
			nei_dist = dist[cur_rid] + 1;
			for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
				nei_rid = it->first;
				if (dist[nei_rid] > nei_dist) {
					dist[nei_rid] = nei_dist;
					q.push(nei_rid);
					next[nei_rid].clear();
					next[nei_rid].push_back(cur_rid);
				} else if (dist[nei_rid] == nei_dist) {
					next[nei_rid].push_back(cur_rid);
				}
			}
		}
		// Step 2: Iterate over all source_rids and add at least one shortest path to the routing table.
		for (int src_rid : _routers_with_nodes) {
			bool at_least_one_path_successfully_assigned = false;
			// No need to compute a path from a router to itself
			if (src_rid == dst_rid) {
				continue;	
			}
			// Iterate over all neighbors of the source routers that are on a shortest path to the destination
			for (int nei_rid : next[src_rid]) {
				// Store this hop in the routing table (independent on whether we are able to successfully assign the paths starting over this hop to a layer or not).
				out_port = get<0>(router_list[1][src_rid][nei_rid]);
				_routing_table[src_rid][dst_rid].push_back(out_port);
				// Try to assign all shortest paths (potentially diverging and converging) to a layer
				layer = 0;
				bool this_path_successfully_assigned = false;
				while (layer < n_vcs) {
					if (are_valid_paths(next, src_rid, nei_rid, dst_rid, layer, false)) {
						this_path_successfully_assigned = true;
						at_least_one_path_successfully_assigned = true;
						break;
					}
					layer++;
				}
				max_layer_used = max(max_layer_used, layer);
				// Configure the layer table based on whether we were able to successfully assign this path to a layer or not.
				if (this_path_successfully_assigned) {
					_layer_map[src_rid][dst_rid][out_port].push_back(layer);
					// Add the edges corresponding to this path to the CDG
					are_valid_paths(next, src_rid, nei_rid, dst_rid, layer, true);
				}
			}
			// If we were able to assign at least one path, we are done, if not, then his routing algorithm is not applicable
			if (!at_least_one_path_successfully_assigned) {
				// If we did not manage to assign any path for this source-destination pair, then this routing algorithm is not applicable for this topology.
				// In order to determine the number of layers that would have been needed, we assign the first path to an non-existing layer
				layer = n_vcs;
				while (true) {
					// Add one more CDG if needed
					if (layer == static_cast<int>(_cdgs.size())) {
						_cdgs.push_back(CDG_E(router_list, n_routers, 1));
					}
					// Check if this layer can accommodate the paths.
					if (are_valid_paths(next, src_rid, next[src_rid][0], dst_rid, layer, false)) {
						break;
					}
					layer++;
				}
				max_layer_used = max(max_layer_used, layer);
				// Add the paths to the CDG of this non-existing layer
				are_valid_paths(next, src_rid, next[src_rid][0], dst_rid, layer, true);
			}
		}
	}
	// If this part of the code is reached, then we were able to successfully assign at least one path for all source-destination pairs to a layer, so we return true.
	return max_layer_used + 1;	// We add 1 since layer is 0-indexed
}


bool a_lash_routing::are_valid_paths(vector<vector<int>> next, int src_rid, int nei_rid, int dst_rid, int layer, bool commit_paths) {
	// Corner case where the path is only one hop
	if (nei_rid == dst_rid) {
		return true;
	}
	// Variables
	int prv_rid, cur_rid;
	int n_dst_reached;
	tuple<int,int,int> from, to;
	vector<pair<tuple<int,int,int>, tuple<int,int,int>>> temporary_cdg_edges;
	set<pair<int,int>> current_hops;
	set<pair<int,int>> next_hops;
	current_hops.insert(make_pair(src_rid, nei_rid));
	while (!current_hops.empty()) {
		next_hops.clear();
		n_dst_reached = 0;
		// Process all current_hops and identify the next_rids on the paths to dst_rid.
		for (pair<int,int> cur_hop : current_hops) {
			prv_rid = cur_hop.first;
			cur_rid = cur_hop.second;
			for (int nxt_rid : next[cur_rid]) { 
				// Count paths that reach the destination for sanity checking
				if (nxt_rid == dst_rid) {
					n_dst_reached++;
				}
				// Check if we can add this edge to the CDG without creating a cycle.
				from = make_tuple(prv_rid, cur_rid, 0);
				to = make_tuple(cur_rid, nxt_rid, 0);
				// If the edge already exists in the CDG, we can skip it since we know it does not induce a cycle.
				if (!_cdgs[layer].has_edge(from, to)) {
					// If the edge creates a cycle in the CDG, this path is not valid for this layer. 
					if (_cdgs[layer].creates_cycle(from, to)) {
						if (commit_paths) {
							cout << "Error in a_lash_routing::are_valid_paths: Trying to commit paths that create a cycle in the CDG. This should not happen since we check for cycles before committing paths." << endl;
							exit(1);
						} else {
							// Remove all temporary edges that we added to the CDG for this path before returning false if we do not commit the paths
							for (pair<tuple<int,int,int>, tuple<int,int,int>> edge : temporary_cdg_edges) {
								_cdgs[layer].remove_edge(edge.first, edge.second);
							}
						}
						return false;
					}
					// If we reached this part of the code, then this edge can be added to the CDG without creating a cycle, so we add it to the CDG
					_cdgs[layer].insert_edge(from, to);
					temporary_cdg_edges.push_back(make_pair(from, to));
				}
				// If we did not reach the destination, we add this hop to the next hops that we need to process in the next iteration of the while loop.
				if (nxt_rid != dst_rid) {
					next_hops.insert(make_pair(cur_rid, nxt_rid));
				}
			}
		}
		// None of the paths reached the destination and we are still finding new next hops -> continue with the next hops
		if (n_dst_reached == 0 and !next_hops.empty()) {
			current_hops = next_hops;
		// All of the paths reached the destination and there are no next hops -> Return true since all paths are valid and reach the destination
		} else if (n_dst_reached == static_cast<int>(current_hops.size()) and next_hops.empty()) {
			// Remove all temporary edges that we added to the CDG for this path before returning true if we do not commit the paths
			if (!commit_paths) {
				for (pair<tuple<int,int,int>, tuple<int,int,int>> edge : temporary_cdg_edges) {
					_cdgs[layer].remove_edge(edge.first, edge.second);
				}
			}
			return true;
		// If none of the two cases above apply, then some paths reached the destination and some did not -> Error
		} else {
			cout << "Error in a_lash_routing::are_valid_paths: Some paths reached the destination and some did not. This should not happen since all paths should have the same length." << endl;
			cout << "n_dst_reached: " << n_dst_reached << ", current_hops.size(): " << current_hops.size() << ", next_hops.size(): " << next_hops.size() << endl;
			exit(1);
		}
	}
	// This part of the code should never be reached since we should have returned true or false in the while loop above. We return false here to avoid compiler warnings about not all control paths returning a value.
	cout << "Error in a_lash_routing::are_valid_paths: Reached end of function without returning true or false. This should not happen since we should have returned true or false in the while loop above." << endl;
	exit(1);
}

void a_lash_routing::try_to_add_paths_to_muliple_layers(int n_routers, int n_vcs) {
	// Iterate over all src-dst pairs
	for (int dst_rid : _routers_with_nodes) {
		// Compute the "next" data structure for this dst_rid from the routing table.
		vector<vector<int>> next(n_routers);
		for (int rid = 0; rid < n_routers; rid++) {
			for (int out_port : _routing_table[rid][dst_rid]) {
				int nei_rid = _router_and_outport_to_next_router[rid][out_port];
				next[rid].push_back(nei_rid);
			}
		}
		// Iterate over all src_rids and try to add the paths to this dst_rid to multiple layers if possible to increase path diversity and potentially reduce congestion.
		for (int src_rid : _routers_with_nodes) {
			// Skip pairs where src and dst are the same since we do not need to route in this case
			if (src_rid == dst_rid) {
				continue;
			}
			//Iterate over all output ports that are currently in the layer map for this src-dst pair
			for (pair<int, vector<int>> out_port_and_layers : _layer_map[src_rid][dst_rid]) {
				int out_port = out_port_and_layers.first;
				int nei_rid = _router_and_outport_to_next_router[src_rid][out_port];
				vector<int> assigned_layers = out_port_and_layers.second;
				// Try to add the paths corresponding to this output port to another layer if possible
				for (int layer = 0; layer < n_vcs; layer++) {
					// If this layer is already used for this src-dst pair, we skip it
					if (find(assigned_layers.begin(), assigned_layers.end(), layer) != assigned_layers.end()) {
						continue;
					}
					// If we can add the paths corresponding to this output port to this layer, we add them and update the layer map accordingly
					if (are_valid_paths(next, src_rid, nei_rid, dst_rid, layer, false)) {
						are_valid_paths(next, src_rid, nei_rid, dst_rid, layer, true);
						_layer_map[src_rid][dst_rid][out_port].push_back(layer);
					}
				}
			}
		}
	}
}

// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> a_lash_routing::get_max_routing_table_size(int N, int P, int V) {
	// Variables to be set based on routing table: C: Max number of routing choices per hop (over all cur_rids, dst_rids), L: Max number of layers per output port (over all cur_rids, dst_rids, out_ports)
	int C = 0, L = 0;
	for (map<int, map<int, vector<int>>>::const_iterator it = _routing_table.begin(); it != _routing_table.end(); ++it) {
		for (map<int, vector<int>>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
			C = max(C, static_cast<int>(it2->second.size()));
		}
	}
	for (map<int, map<int, map<int, vector<int>>>>::const_iterator it = _layer_map.begin(); it != _layer_map.end(); ++it) {
		for (map<int, map<int, vector<int>>>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
			for (map<int, vector<int>>::const_iterator it3 = it2->second.begin(); it3 != it2->second.end(); ++it3) {
				L = max(L, static_cast<int>(it3->second.size()));
			}
		}
	}
	// Compute table size based on variables identified above
	int dense_rows = N;
	int bits_list = C * ceil(log2(P));
	int bits_bitmap = P;
	int csr_it_rows = N;
	int csr_it_bits = ceil(log2(N));
	int csr_rows = N;

	int layer_table_rows = N * P; 										// We assume a dense layer table and do not explore the layer table store as CSV.
	int layer_table_bits = min(V, (int)(L * ceil(log2(V)))); 					// We compute layer table size as list and as bitmap and we take the smaller of the two.
	int layer_table = layer_table_rows * layer_table_bits;

	int dense_list = dense_rows * bits_list + layer_table;
	int dense_bitmap = dense_rows * bits_bitmap + layer_table;
	int csr_list = csr_it_rows * csr_it_bits + csr_rows * bits_list + layer_table;
	int csr_bitmap = csr_it_rows * csr_it_bits + csr_rows * bits_bitmap + layer_table;
	return make_tuple(dense_list, dense_bitmap, csr_list, csr_bitmap);
}

