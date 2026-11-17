#include "lash_tor_routing.hpp"
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
#include <random>

using namespace std;

// Constructor
lash_tor_routing::lash_tor_routing(const Configuration config, std::unique_ptr<routing_function> fallback, vector<int> routers_with_nodes, vector<map<int,int>> router_and_outport_to_next_router) : _fallback_routing_function(std::move(fallback)) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_n_layers = config.GetInt("n_layers");
	_is_initialized = false;
	_routers_with_nodes = routers_with_nodes;
	_router_and_outport_to_next_router = router_and_outport_to_next_router;
	_additional_info = 0;
}

// Initialization which is only called once per simulation and which computes all routing tables.
void lash_tor_routing::initialize(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers) {
	cout << "Initializing LASH-TOR routing..." << endl;
	// Step 1: Try to use all shortest paths by allowing LASH-TOR to use all VCs, but abort if some paths cannot be assigned.
	cout << "- Trying to find and commit paths using all VCs without reserving a layer for the fallback routing function..." << endl;
	bool success = find_and_commit_paths(router_list, n_routers, n_nodes, n_layers, false);
	// If configuring the base routing succeeded, we are done, otherwise, we need to recompute paths using the fallback routing layer.
	if (!success){
		cout << "- Failed to find and commit paths for all source-destination pairs using all VCs." << endl;
		cout << "- Trying to find and commit paths while reserving one layer for the fallback routing function..." << endl;
		// Clear routing tables and CDG
		_routing_table.clear();
		_cdgs.clear();
		// Compute paths again, this time with one layer reserved for the fallback routing function.
		find_and_commit_paths(router_list, n_routers, n_nodes, n_layers, true);
		cout << "- Successfully found and committed paths for all source-destination pairs while reserving one layer for the fallback routing function." << endl;
		_additional_info = 2;
	} else {
		cout << "- Successfully found and committed paths for all source-destination pairs using all VCs without reserving a layer for the fallback routing function." << endl;
		_additional_info = 1;
	}
}

vector<tuple<int,int,int>> lash_tor_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	int in_layer = (in_vc * _n_layers) / n_vcs;
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this 
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "lash_tor_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + "_" + to_string(_n_layers) + "layers" + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType5 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			_additional_info = rt._additional_info;
			cout << "- Loaded LASH-TOR routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<lash_tor_routing*>(this)->initialize(router_list, n_routers, n_nodes, _n_layers);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType5 rt;
				rt._routing_table = _routing_table;
				rt._additional_info = _additional_info;
				rt.save(rt_cache_file);
				cout << "Saved LASH-TOR routing tables to cache: " << rt_cache_file << endl;
			}
		}
		_is_initialized = true;
		cout << "Finished initializing LASH-TOR routing." << endl;
	}
	int output_port, output_vc, output_layer, layer_start, layer_end;
	vector<tuple<int,int,int>> valid_next_hops;
	// If we are already at the destination router, we can directly route to the destination node
	if (cur_rid == dst_rid) {	
		output_port = get<0>(router_list[0][cur_rid][dst_nid]);						// Identify output port to dst_nid
		valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs - 1));			// We can us all VCs for ejection.
	} else {
		// If we are at the source (injection), we replace the real input-port with -1 to indicate that we are injecting.
		if (cur_rid == src_rid) {
			in_port = -1;	// Use -1 to indicate injection
			in_layer = 0; 		// Use VC 0 for injection
		}
		// Read next hops from routing table. Format is (output_port, output_layer, path_length, next_rid)
        tuple<int,int,int> next_hop = _routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)];
		output_port = get<0>(next_hop);
		output_layer = get<1>(next_hop);
		layer_start = (output_layer * n_vcs) / _n_layers;
		layer_end = ((output_layer + 1) * n_vcs) / _n_layers;
		for (output_vc = layer_start; output_vc < layer_end; output_vc++) {
			valid_next_hops.push_back(make_tuple(output_port, output_vc, output_vc));
		}
	}
	return valid_next_hops;
}

bool lash_tor_routing::find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, bool use_fallback_layer) {
	// Variables
	int prv_rid, cur_rid, nei_rid, nxt_rid;
	int nei_dist;
	int cur_layer, nei_layer;
	int in_port, out_port;
	int dst_nid;
	int in_layer, out_layer;
	// Initialize one CDG per layer
	for (int layer = 0; layer < n_layers; layer++) {
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
		// Step 2: Iterate over all source_rids and select the shortest path, that needs the least number of layer-transitions
		for (int src_rid : _routers_with_nodes) {
			if (src_rid == dst_rid) {
				continue;	// No need to compute a path from a router to itself
			}
			// Find the minimal path from src_rid to dst_rid but this time, minimize the number of layer-transitions
			vector<int> prev(n_routers, -1);
			vector<int> layer(n_routers, INT_MAX);
			// We need a priority queue here to ensure that each vertex is only visited once with the minimal number of layer transitions,
			// Otherwise, we might change the prev-hops which changes the paths and invalidates the CDG-based checks.
			priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;	// (number of layer transitions, router ID)
			layer[src_rid] = 0;
			pq.push(make_pair(0, src_rid));
			while (!pq.empty()) {
				cur_layer = pq.top().first;
				cur_rid = pq.top().second;
				pq.pop();
				// We compute the path on the current layer.
				vector<int> path;
				int temp_rid = cur_rid;
				while (temp_rid != -1 and layer[temp_rid] == cur_layer) {
					path.push_back(temp_rid);
					temp_rid = prev[temp_rid];
				}
				reverse(path.begin(), path.end());
				// When analyzing neighbors, we consider both, no layer transition and a layer transition to the subsequent layer
				for (bool use_layer_transition : {false, true}) {
					nei_layer = cur_layer + (use_layer_transition ? 1 : 0);
					// Iterate over all neighbors (only on shortest paths) 
					for (int nei_rid : next[cur_rid]) {
						// Layer transition: If we exceeded the maximum available layer for non-fallback routing, we cannot use this neighbor on a new layer.
						if (nei_layer >= (n_layers - (use_fallback_layer ? 1 : 0))){
							continue;
						}
						// No layer transition: If going to this neighbor on the current layer induces a cycle in the CDG, we cannot use this neighbor on this layer.
						if (!use_layer_transition and !is_valid_hop(path, nei_rid, nei_layer)) {
							continue;	
						}
						// If we reached this part of the code, then nei_rid can be reached from cur_rid without inducing a cycle in the CDG or exceeding the maximum number of layers.
						if (nei_layer < layer[nei_rid]) {
							layer[nei_rid] = nei_layer;
							prev[nei_rid] = cur_rid;
							pq.push(make_pair(nei_layer, nei_rid));
						}
					}	
				}
			}
			bool path_found = (prev[dst_rid] != -1);
			// If we did not find a path and we are not using a fallback layer, we abort and return false
			if (!path_found and !use_fallback_layer) {
				return false;
			}
			// Build a next map based on the prev map to easily identify the next hop on the path from src_rid to dst_rid.
			vector<int> next_hop(n_routers, -1);
			if (path_found) {
				cur_rid = dst_rid;
				while (cur_rid != src_rid) {
					next_hop[prev[cur_rid]] = cur_rid;
					cur_rid = prev[cur_rid];
				}
			}
			// Otherwise, we configure the path (either using fallback routing or using the shortest path with minimal layer transitions that we just found above)
			prv_rid = -1;
			cur_rid = src_rid;
			dst_nid = router_list[0][dst_rid].begin()->first;
			in_port = get<1>(router_list[0][cur_rid].begin()->second);
			in_layer = 0;
			while (cur_rid != dst_rid) {
				// Select next hop either based on the path we found above or based on the fallback routing function if we did not find a path above.
				if (!path_found) {
					tuple<int,int,int> valid_hops_rf = _fallback_routing_function->route(router_list, n_routers, n_nodes, 1, src_rid, cur_rid, 0, dst_nid, dst_rid, in_port)[0]; // Use firs choice if RF returns multiple choices.
					out_port = get<0>(valid_hops_rf);
					nxt_rid = _router_and_outport_to_next_router[cur_rid][out_port];
					out_layer = n_layers - 1;	// Use the last VC for the fallback routing layer
				} else {
					nxt_rid = next_hop[cur_rid];
					out_port = get<0>(router_list[1][cur_rid][nxt_rid]);
					out_layer = layer[nxt_rid];
				}
				// Configure the routing table for this hop on the fallback layer
				// We set the in_port to -1 for injection
				in_port = (prv_rid == -1) ? -1 : in_port;
				_routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)] = make_tuple(out_port, out_layer, nxt_rid);
				// Update CDG for this hop on the fallback layer (unless we are at the source router or we moved to a different layer)
				if (cur_rid != src_rid and out_layer == in_layer) {
					// This will internally check if the edge creates a cycle and throw an error if this is the case
					// If the edge already exists, it will not be added again and no error will be thrown.
					_cdgs[in_layer].insert_edge(make_tuple(prv_rid, cur_rid, 0), make_tuple(cur_rid, nxt_rid, 0));
				}
				// Update variables for next hop
				in_port = get<1>(router_list[1][cur_rid][nxt_rid]);
				in_layer = out_layer;
				prv_rid = cur_rid;
				cur_rid = nxt_rid;
			}
		}
	}
	// Step 3: Balance paths over layers (this is only performed if we have more than one VC)
	if (n_layers <= 1) {
		return true;	// No need to balance paths over layers if we only have one layer
	}
	cout << "-- Balancing paths over layers..." << endl;
	vector<vector<int>> paths_on_lowerst_layer;
	vector<int> hops_per_layer(n_layers, 0);
	for (int src_rid : _routers_with_nodes) {
		for (int dst_rid : _routers_with_nodes) {
			// No path from a router to itself
			if (src_rid == dst_rid) {
				continue;
			}
			// Check if this path stays at the lowest layer and collect statistics on number of hops per layer
			bool on_lowest_layer = true;
			vector<int> path;
			cur_rid = src_rid;
			in_port = -1;
			in_layer = 0;
			while (cur_rid != dst_rid) {
				if (_routing_table[cur_rid][dst_rid].find(make_pair(in_port, in_layer)) == _routing_table[cur_rid][dst_rid].end()) {
					cout << "Error: No routing entry for cur_rid = " << cur_rid << ", dst_rid = " << dst_rid << ", in_port = " << in_port << ", in_layer = " << in_layer << endl;
					exit(1);
				}
				tuple<int,int,int> next_hop = _routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)];
				out_port = get<0>(next_hop);
				out_layer = get<1>(next_hop);
				nxt_rid = get<2>(next_hop);
				// Add hop to path
				path.push_back(cur_rid);
				// Check if the path is still on the lowest layer.
				if (out_layer != 0) {
					on_lowest_layer = false;
				}
				// Update hops per layer statistics
				hops_per_layer[out_layer]++;
				in_port = get<1>(router_list[1][cur_rid][nxt_rid]);
				in_layer = out_layer;
				cur_rid = nxt_rid;
			}
			path.push_back(dst_rid);
			// If this path stays on the lowest layer, we add it to the list of paths on the lowest layer.
			if (on_lowest_layer) {
				paths_on_lowerst_layer.push_back(path);
			}
		}
	}
	// The original balancing algorithm would sort layers based on number of hops per layer in descending order
	// and use that order to move paths. We found that in our experiments, this often leads to most paths being moved
	// To exactly the same layer, moving the congestion from the lowest to the highers layer.
	// So instead, we balance uniformly among all layers that have fewer than half the hops of the lowest layer
	static std::mt19937 rng(std::random_device{}());
	vector<int> candidate_layers;
	for (int layer = 1; layer < n_layers - (use_fallback_layer ? 1 : 0); layer++) {
		if (hops_per_layer[layer] <= hops_per_layer[0] / 2) {
			candidate_layers.push_back(layer);
		}
	}
	// Iterate over all paths on the lowest layer and try to move them to higher layers
	for (vector<int> path : paths_on_lowerst_layer) {
		// Shuffle the candidate layers to achieve a more uniform balancing among them.
		shuffle(candidate_layers.begin(), candidate_layers.end(), rng);
		int src_rid = path[0];
		int dst_rid = path.back();
		// Iterate over all layers in descending order of hops per layer and try to move the path to the first layer where this is possible without inducing cycles in the CDG.
		// Note that we do not change the order of layers after reassigning paths since moving one path might change many converging paths as well.
		// The LASH-TOR paper is not fully clear on how the path balancing is done, so we do this best-effort based on our understanding of the paper.
		for (int layer : candidate_layers) {
			if (is_valid_path(path, layer)) {
				prv_rid = -1;
				cur_rid = src_rid;
				in_port = get<1>(router_list[0][src_rid].begin()->second);
				in_layer = -1;
				for (int i = 1; i < static_cast<int>(path.size()); i++) {
					nxt_rid = path[i];
					out_port = get<0>(router_list[1][cur_rid][nxt_rid]);
					// Add the new path to the routing table on the new layer
					// This overwrites the existing entry at the source router
					// We can not simply follow the old path and remove the entries since there might be converging paths that use the same hops.
					_routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)] = make_tuple(out_port, layer, nxt_rid);
					// If we are not at the source router
					if (cur_rid != src_rid) {
						// 1) Add the new edge to the CDG on the new layer
						// This will internally check if the edge creates a cycle and throw an error if this is the case
						// If the edge already exists, it will not be added again and no error will be thrown.
						_cdgs[layer].insert_edge(make_tuple(prv_rid, cur_rid, 0), make_tuple(cur_rid, nxt_rid, 0));
						// We could also remove edges from the CDG of layer 0, but this is not necessary since we do not need the CDG of layer 0 after initialization.
					}
					// Update variables for next hop
					in_port = get<1>(router_list[1][cur_rid][nxt_rid]);
					in_layer = layer;
					prv_rid = cur_rid;
					cur_rid = nxt_rid;
				}
				// If we successfully moved this path to a higher layer, we can stop iterating over layers for this path and move to the next path.
				break;
			}
		}
	}
	cout << "-- Finished balancing paths over layers." << endl;
	// If we reached this part of the code, we successfully found and committed paths for all source-destination pairs, so we can return true.
	return true;
}


bool lash_tor_routing::is_valid_path(vector<int> path, int layer){
	tuple<int,int,int> from, to;
	vector<pair<tuple<int,int,int>, tuple<int,int,int>>> temporary_cdg_edges;
	for (int i = 1; i + 1 < static_cast<int>(path.size()); i++) {
		from = make_tuple(path[i-1], path[i], 0);
		to = make_tuple(path[i], path[i+1], 0);
		// If the edge already exists in the CDG, we can skip it since we know it does not induce a cycle.
		// Note that in this case, we do not add it to the list of temporary edges since we do not want to remove it in the end.
		if (_cdgs[layer].has_edge(from, to)) {
			continue;	
		}
		// If the edge creates a cycle in the CDG, this path is not valid for this layer.
		if (_cdgs[layer].creates_cycle(from, to)) {
			// Remove all temporary edges that we added to the CDG for this path before returning false.
			for (pair<tuple<int,int,int>, tuple<int,int,int>> edge : temporary_cdg_edges) {
				_cdgs[layer].remove_edge(edge.first, edge.second);
			}
			return false;
		}
		// If we reached this part of the code, then this edge can be added to the CDG without creating a cycle
		_cdgs[layer].insert_edge(from, to);
		temporary_cdg_edges.push_back(make_pair(from, to));
	}
	// If we reached this part of the code, then all edges of this path can be added to the CDG without creating a cycle, so this path is valid for this layer.
	// Remove all temporary edges that we added to the CDG for this path before returning true, since we only wanted to check if the path is valid but we do not want to actually add it to the CDG at this point.
	for (pair<tuple<int,int,int>, tuple<int,int,int>> edge : temporary_cdg_edges) {
		_cdgs[layer].remove_edge(edge.first, edge.second);
	}
	return true;
}

bool lash_tor_routing::is_valid_hop(vector<int> path, int nxt_rid, int layer){
	// Temporarily add nxt_rid to the path and use the is_valid_path function to check if the new path is valid.
	path.push_back(nxt_rid);
	bool result = is_valid_path(path, layer);
	path.pop_back();
	return result;
}

int lash_tor_routing::get_additional_info() const {
	return _additional_info;
}

// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> lash_tor_routing::get_max_routing_table_size(int N, int P, int V) {
	// Variables to be set based on routing table: E: Max number of entries in the routing table (over all cur_rids)
	int E = 0;
	// Number of destinations equals number of routers with nodes
	for (map<int, map<int, map<pair<int,int>, tuple<int,int,int>>>>::const_iterator it = _routing_table.begin(); it != _routing_table.end(); ++it) {
		int E_tmp = 0;
		for (map<int, map<pair<int,int>, tuple<int,int,int>>>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
			for (map<pair<int,int>, tuple<int,int,int>>::const_iterator it3 = it2->second.begin(); it3 != it2->second.end(); ++it3) {
				E_tmp++;
			}
		}
		E = max(E, E_tmp);
	}
	// In LASH-TOR, the routing table size depends on the number of layers, not the number of VCs, hence, we overwrite V with _n_layers for the calculation of the routing table size.
    V = _n_layers;
	// Compute table size based on variables identified above
	int dense_rows = N * (P * V + 1);
	int bits_list = ceil(log2(P)) + ceil(log2(V));
	int bits_bitmap = P * V;
	int csr_it_rows = N * (P * V + 1);
	int csr_it_bits = ceil(log2(E));
	int csr_rows = E;
	int dense_list = dense_rows * bits_list;
	int dense_bitmap = dense_rows * bits_bitmap;
	int csr_list = csr_it_rows * csr_it_bits + csr_rows * bits_list;
	int csr_bitmap = csr_it_rows * csr_it_bits + csr_rows * bits_bitmap;
	return make_tuple(dense_list, dense_bitmap, csr_list, csr_bitmap);
}
