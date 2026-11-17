#include "dfsssp_routing.hpp"
#include "path_based_cdg.hpp"
#include "topology_hash.hpp"
#include "routing_tables.hpp"
#include "../config_utils.hpp"
#include <filesystem>
#include <queue>
#include <vector>
#include <iostream>
#include <algorithm>
#include <tuple>
#include <vector>
#include <map>
#include <set>
#include <climits>
#include <cmath>

using namespace std;

// Constructor
dfsssp_routing::dfsssp_routing(const Configuration config, vector<int> rouers_with_nodes, vector<map<int,int>> router_and_outport_to_next_router) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
    _is_initialized = false;
	_routers_with_nodes = rouers_with_nodes;
	_router_and_outport_to_next_router = router_and_outport_to_next_router;
}

void dfsssp_routing::initialize(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_vcs) const {
	// Step 1: Compute shortest paths according to Algorithm 1 in the DFSSSP paper. All paths will be assigned to layer 0, potentially causing deadlocks.
	find_and_commit_paths(router_list, n_routers);
	// Step 2: Assign paths to layers according to Algorithm 2 in the DFSSSP paper. This step ensures that the resulting routing algorithm is deadlock-free.
	search_and_remove_deadlocks(router_list, n_routers, n_vcs);
	// If we need more layers than we have VCs available, we cannot route all paths and need to exit with an error.
    if (_n_layers > n_vcs) {
        cout << "Error in dfsssp_routing::initialize: The number of layers needed to route all paths (" << _n_layers << ") exceeds the number of available VCs (" << n_vcs << ")" << endl;
        exit(4);	// Return code 4 is "Insufficient VCs"
    } else {
        cout << "Configured DFSSSP routing with " << _n_layers << " layers (" << n_vcs << " VCs available)." << endl;
        // Configure the VC map to map the layer to the corresponding VCs.
        // If there are more VCs than we need layers, we assign the remaining VCs to layers in a round-robin manner.
        for (int vc = 0; vc < n_vcs; vc++) {
            int layer = vc % _n_layers;
            _vc_map[layer].push_back(vc);
        }
    }
	// Mark as initialized
	_is_initialized = true;
}


// Compute shortest paths according to Algorithm 1 in the DFSSSP paper. All paths will be assigned to layer 0, potentially causing deadlocks.
void dfsssp_routing::find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers) const {
	// Initialize the edge weights as described in DFSSSP paper (page 2, bottom-right).
	int init_weight = n_routers * n_routers;
	map<pair<int,int>, int> edge_weights;
	for (int rid = 0; rid < n_routers; rid++) {
		for (const pair<const int, tuple<int,int,int>>& item : router_list[1][rid]) {
			edge_weights[make_pair(rid, item.first)] = init_weight;
		}
	}
	// Variables reused throughout the algorithm
	int cur_rid, cur_dist, nxt_rid;
	// Iterate through all destination routers that are connected to nodes and compute shortest paths to them
	for (int dst_rid : _routers_with_nodes) {
		priority_queue<pair<int,int>, vector<pair<int,int>>, greater<>> queue;
		vector<int> dist(n_routers, INT_MAX);
		vector<int> next(n_routers, -1);
		dist[dst_rid] = 0;
		queue.push({0, dst_rid}); // distance, router-ID
		// Dijkstra's algorithm to find shortest paths to a given dst_rid w.r.t. edge weights.
		while (!queue.empty()) {
			// Get the router with the smallest distance to dst_rid from the priority queue
			pair<int,int> current = queue.top();
			queue.pop();
			cur_dist = current.first;
			cur_rid = current.second;
			// Skip in case this element is outdated (i.e. we have already found a shorter path to cur_rid)
			if (cur_dist > dist[cur_rid]) {
				continue;
			}
			// Iterate through all neighbors of cur_rid
			for (const pair<const int, tuple<int,int,int>>& item : router_list[1][cur_rid]) {
				nxt_rid = item.first;
				pair<int,int> edge = make_pair(cur_rid, nxt_rid);
				if (dist[cur_rid] + edge_weights[edge] < dist[nxt_rid]) {
					dist[nxt_rid] = dist[cur_rid] + edge_weights[edge];
					next[nxt_rid] = cur_rid;
					queue.push({dist[nxt_rid], nxt_rid}); // Add pair with updated distance to priority queue
				}
			}
		}
		// Set routing table entries for all routers that can reach dst_rid and update edge weights accordingly
		for (int src_rid : _routers_with_nodes) {
			// If we are already at the destination router, the route() function will directly route to the destination node without using the routing table.
			if (src_rid == dst_rid){
				continue;
			}
			// If src_rid cannot reach dst_rid, we throw an error as the network is not fully connected.
			if (dist[src_rid] == INT_MAX) {
				cout << "Error: Router " << src_rid << " cannot reach destination router " << dst_rid << ". The network is not fully connected." << endl;
				exit(1);
			}
			_routing_table[src_rid][dst_rid] = get<0>(router_list[1][src_rid][next[src_rid]]);
		}
	}
}

// Assign paths to layers according to Algorithm 2 in the DFSSSP paper. This step ensures that the resulting routing algorithm is deadlock-free.
void dfsssp_routing::search_and_remove_deadlocks(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_vcs) const {
	// Variables reused throughout the algorithm
	tuple<int,int,int> hop;
	int cur_rid, src_rid, dst_rid, nxt_rid;
	int out_port;
	// Initialize the CDG and the layer_map with all paths (initially on layer 0)
	// Here, we use the pah-based CDG instead of the more commonly used edge-based CDG.
	CDG_P cdg = CDG_P(router_list, n_routers, n_vcs);
	for (int src_rid : _routers_with_nodes) {
		for (int dst_rid : _routers_with_nodes) {
			if (src_rid != dst_rid) {
				_layer_map[src_rid][dst_rid] = 0; // Initially, all paths are on layer 0
				vector<tuple<int,int,int>> path;
				cur_rid = src_rid;
				while (cur_rid != dst_rid) {
					out_port = _routing_table[cur_rid][dst_rid];
					nxt_rid = _router_and_outport_to_next_router[cur_rid][out_port];
					path.push_back(make_tuple(cur_rid, nxt_rid, 0)); // Initially, all paths are on layer 0
					cur_rid = nxt_rid;
				}
				cdg.insert_path(path);
			}
		}
	}
	int max_vc = 0;
	// Move paths for the current to the next virtual layer until there are no more cycles in the CDG that use the current virtual layer.
	while (cdg.is_cyclic()){
		vector<vector<tuple<int,int,int>>> paths_to_move =  cdg.identify_one_cycle_and_remove_min_number_of_paths_to_break_it();
		// In DFSSSP, a path should always be on just one layer, hence, all hops of all removed paths should be on the same layer. Verify this.
		int cur_vc = -1;
		for (vector<tuple<int,int,int>>& path : paths_to_move) {
			for (tuple<int,int,int>& hop : path) {
				if (cur_vc == -1) {
					cur_vc = get<2>(hop);
				} else if (get<2>(hop) != cur_vc) {
					cout << "DFSSSP: Error: Identified paths for cycle breaking are not all on the same layer. This should not happen in DFSSSP." << endl;
					exit(1);
				}
			}
		}
		int new_vc = cur_vc + 1;
		if (new_vc >= n_vcs && new_vc > max_vc) {
			// Add an extra VC to the CDG even though this one does not exist in the actual network.
			// This is only done to identify the number of required VCs for deadlock-free routing.
			// In those cases, BookSim will be aborted after initialization with an error message.
			cdg.increase_vcs(1);	
		}
		max_vc = max(max_vc, new_vc);
		// Move all identified paths to the next virtual layer update the CDG accordingly.
		for (vector<tuple<int,int,int>>& path : paths_to_move) {
			src_rid = get<0>(path[0]);
			dst_rid = get<1>(path[path.size() - 1]);
			// Edit the layer map
			_layer_map[src_rid][dst_rid] = new_vc;
			// Edit the paths directly. This is used to update the CDG below
			for (tuple<int,int,int>& hop : path) {
				get<2>(hop) = new_vc;
			}
			// Insert the updated path with the new virtual layer into the CDG
			cdg.insert_path(path);
		}
	}
	// Set number of layers used.
	_n_layers = max_vc + 1;
	// Note that Algorithm 2 in the DFSSSP paper also has a step to balance paths by moving some paths to empty virtual layers.
	// Since we treat virtual layers as not the same as virtual channels, this is not needed here.
	// To ensure that all VCs are used, we allow assignment of multiple VCs to the same layer (this is done in the initialize() function after this function has been called).
}

vector<tuple<int,int,int>> dfsssp_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
    // Initialize, i.e. compute routing tables
    if (!_is_initialized) {
        // First, check if a routing table for this topology and configuration is already stored in the cache.
        size_t topology_hash = router_hash::hash_router_list(router_list);
        string rt_cache_file = _cache_directory + "dfsssp_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + "_" + to_string(n_vcs) + "vcs" + ".bin";
        cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
        if (filesystem::exists(rt_cache_file)) {
            // Load routing tables from cache
            RoutingTableType4 rt;
            rt.load(rt_cache_file);
            _routing_table = rt._routing_table;
            _layer_map = rt._layer_map;
            _vc_map = rt._vc_map;
            _n_layers = _vc_map.size();
            cout << "- Loaded DFSSSP routing tables from cache: " << rt_cache_file << endl;
        } else {
            cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
            // Hack to call non-constant initialize function from constant route function.
            const_cast<dfsssp_routing*>(this)->initialize(router_list, n_routers, n_vcs);
            // Store routing tables in cache for future runs (if not already stored by a different thread)
            if (!filesystem::exists(rt_cache_file)) {
                RoutingTableType4 rt;
                rt._routing_table = _routing_table;
                rt._layer_map = _layer_map;
                rt._vc_map = _vc_map;
                rt.save(rt_cache_file);
                cout << "Saved DFSSSP routing tables to cache: " << rt_cache_file << endl;
            }
        }
        _is_initialized = true;
    }
    vector<tuple<int,int,int>> valid_next_hops;
    int output_port;
    // If we are already at the destination router, we can directly route to the destination node
    if (cur_rid == dst_rid) {   
        output_port = get<0>(router_list[0][cur_rid][dst_nid]);                     // Identify output port to dst_nid
		valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs - 1));			// We can us all VCs for ejection.
    } else {
        // Read the output port from the routing table
        output_port = _routing_table[cur_rid][dst_rid];
        // If we are injecting, we need to send the packet to the correct VC, otherwise, the VC stays as is.
        if (cur_rid == src_rid) {
            int layer = _layer_map[src_rid][dst_rid];
            // Add all available layers s.t. the selection function can select between them for load-balancing.
            for (int vc : _vc_map[layer]) {
                valid_next_hops.push_back(make_tuple(output_port, vc, vc));         // Potentially multiple available VCs for injection.
            }
        } else {
            valid_next_hops.push_back(make_tuple(output_port, in_vc, in_vc));       // Output port and same VC for forwarding
        }
    }
    return valid_next_hops;
}


// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> dfsssp_routing::get_max_routing_table_size(int N, int P, int V) {
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
