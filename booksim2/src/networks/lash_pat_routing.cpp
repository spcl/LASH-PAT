#include "lash_pat_routing.hpp"
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
#include <random>

using namespace std;

// Cost of 1.0 per hop
double lash_pat_routing::cost_function_length(int from_rid, int to_rid, int layer, int n_routers) const {
	double cost = 1.0;
	return cost;
}

// Cost of 1.0 per hop and a penalty that favours lower VCs in order to keep higher VCs available as long as possible.
double lash_pat_routing::cost_function_length_and_layer(int from_rid, int to_rid, int layer, int n_routers) const {
	double cost = 1.0 + (layer / (1.0 * _n_layers * n_routers));
	return cost;
}

// Cost of 1.0 per hop and a penalty of 0.0 <= penalty < 1.0 based on how often a give physical channel  has already been used
double lash_pat_routing::cost_function_length_and_pc_utilization(int from_rid, int to_rid, int layer, int n_routers) const {
	double cost = 1.0; // Cost of 1 for the distance
	if (_paths_per_physical_channel.find(make_pair(from_rid, to_rid)) != _paths_per_physical_channel.end()) {
		cost += _paths_per_physical_channel[make_pair(from_rid, to_rid)] / (1.0 * n_routers * n_routers * n_routers);
	}
	return cost;
}

// Cost of 1.0 per hop and a penalty of 0.0 <= penalty < 1.0 based on how often a give VC has already been used
double lash_pat_routing::cost_function_length_and_vc_utilization(int from_rid, int to_rid, int layer, int n_routers) const {
	double cost = 1.0; // Cost of 1 for the distance
	if (_paths_per_virtual_channel.find(make_tuple(from_rid, to_rid, layer)) != _paths_per_virtual_channel.end()) {
		cost += _paths_per_virtual_channel[make_tuple(from_rid, to_rid, layer)] / (1.0 * n_routers * n_routers * n_routers);
	}
	return cost;
}

// Cost of 1.0 per hop and a penalty of 0.0 <= penalty < 1.0 based on how often a give VC and PC has already been used
double lash_pat_routing::cost_function_length_and_combined_utilization(int from_rid, int to_rid, int layer, int n_routers) const {
	double cost = 1.0; // Cost of 1 for the distance
	if (_paths_per_physical_channel.find(make_pair(from_rid, to_rid)) != _paths_per_physical_channel.end()) {
		cost += _paths_per_physical_channel[make_pair(from_rid, to_rid)] / (2.0 * n_routers * n_routers * n_routers);
	}
	if (_paths_per_virtual_channel.find(make_tuple(from_rid, to_rid, layer)) != _paths_per_virtual_channel.end()) {
		cost += _paths_per_virtual_channel[make_tuple(from_rid, to_rid, layer)] / (2.0 * n_routers * n_routers * n_routers);
	}
	return cost;
}


// Constructor
lash_pat_routing::lash_pat_routing(const Configuration config, std::unique_ptr<routing_function> fallback, vector<int> routers_with_nodes, vector<map<int,int>> router_and_outport_to_next_router) : _fallback_routing_function(std::move(fallback)) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_n_additional_paths = config.GetInt("lash_pat_n_additional_paths");
	_existing_path_bias = config.GetFloat("lash_pat_existing_path_bias");
	_cost_function_base_type = config.GetStr("lash_pat_cost_function_base");
	_cost_function_multipath_type = config.GetStr("lash_pat_cost_function_multipath");
	_n_layers = config.GetInt("n_layers");
	_cost_aggregation = config.GetStr("lash_pat_cost_aggregation");
	_is_initialized = false;
    _is_in_multipath_phase = false; 
	_additional_info = 0;
	// Set base and multipath cost functions
	if (_cost_function_base_type == "length" or _cost_function_base_type == "cdg") {
		_cost_function_base = &lash_pat_routing::cost_function_length;
	} else if (_cost_function_base_type == "phys_channel") {
		_cost_function_base = &lash_pat_routing::cost_function_length_and_pc_utilization;
	} else if (_cost_function_base_type == "virt_channel") {
		_cost_function_base = &lash_pat_routing::cost_function_length_and_vc_utilization;
	} else if (_cost_function_base_type == "comb_channel" or _cost_function_base_type == "all" or _cost_function_base_type == "red_all") {
		_cost_function_base = &lash_pat_routing::cost_function_length_and_combined_utilization;
	} else if (_cost_function_base_type == "layer") {
		_cost_function_base = &lash_pat_routing::cost_function_length_and_layer;
	} else {
		cout << "Error in lash_pat_routing constructor: Invalid cost function type specified in configuration: " << _cost_function_base_type << ". Aborting." << endl;
		exit(1);
	}
	if (_cost_function_multipath_type == "length" or _cost_function_multipath_type == "cdg") {
		_cost_function_multipath = &lash_pat_routing::cost_function_length;
	} else if (_cost_function_multipath_type == "phys_channel") {
		_cost_function_multipath = &lash_pat_routing::cost_function_length_and_pc_utilization;
	} else if (_cost_function_multipath_type == "virt_channel") {
		_cost_function_multipath = &lash_pat_routing::cost_function_length_and_vc_utilization;
	} else if (_cost_function_multipath_type == "comb_channel" or _cost_function_multipath_type == "all" or _cost_function_multipath_type == "red_all") {
		_cost_function_multipath = &lash_pat_routing::cost_function_length_and_combined_utilization;
	} else if (_cost_function_multipath_type == "layer") {
		_cost_function_multipath = &lash_pat_routing::cost_function_length_and_layer;
	} else {
		cout << "Error in lash_pat_routing constructor: Invalid cost function type specified in configuration: " << _cost_function_multipath_type << ". Aborting." << endl;
		exit(1);
	}
	// Randomly shuffle the routers with nodes to ensure the paths are processed in random order.
	_routers_with_nodes_1 = routers_with_nodes;
	_routers_with_nodes_2 = routers_with_nodes;
	random_device rd;
	mt19937 g(rd());
	shuffle(_routers_with_nodes_1.begin(), _routers_with_nodes_1.end(), g);
	shuffle(_routers_with_nodes_2.begin(), _routers_with_nodes_2.end(), g);
	// Store this auxiliary information
	_router_and_outport_to_next_router = router_and_outport_to_next_router;
}

// Clears routing tables and stats. This is needed if the custom base routing fails and we need to fall back to the external base routing.
void lash_pat_routing::clear_routing_table_and_stats() {
	_routing_table.clear();
	_paths_per_physical_channel.clear();
	_paths_per_virtual_channel.clear();
	_paths_per_virtual_channel_per_dst.clear();
}

// Valid hops are returned in the format [(next_router_id, layer), ...].
vector<pair<int,int>> lash_pat_routing::get_valid_hops_using_fallback_routing(vector<map<int,map<int,tuple<int,int,int>>>> router_list, CDG_E* cdg, int n_routers, int n_nodes, int n_layers, int src_rid, vector<tuple<int,int,int>> prv_hops, int cur_rid, int in_layer, int dst_nid, int dst_rid, int in_port) const {
	// As with our custom routing, we compute a list of previous router and we forbid all next hops that would lead back to any of them.
	// This also avoids 180 degree turns.
	set<int> routers_on_current_paths;
	if (prv_hops.size() > 0) {
		routers_on_current_paths.insert(get<0>(prv_hops[0]));
		for (int i = 0; i < static_cast<int>(prv_hops.size()); i++) {
			routers_on_current_paths.insert(get<1>(prv_hops[i]));
		}
	}
	vector<pair<int,int>> valid_hops;
	// Internally, we use -1 to indicate injection, but the fallback routing function expects a real input port. 
	int real_in_port = (in_port == -1) ? (get<1>(router_list[0][cur_rid].begin()->second)) : in_port;
	vector<tuple<int,int,int>> valid_hops_rf = _fallback_routing_function->route(router_list, n_routers, n_nodes, n_layers, src_rid, cur_rid, in_layer, dst_nid, dst_rid, real_in_port);
	for (tuple<int,int,int> valid_hop_rf: valid_hops_rf) {
		int nxt_rid = _router_and_outport_to_next_router[cur_rid][get<0>(valid_hop_rf)];
		// Exclude all routers on current paths. This avoids 180 degree turns and other loops which make no sense since we don't want to route in circles.
		if (routers_on_current_paths.find(nxt_rid) != routers_on_current_paths.end()) {
			continue; 
		}
		// Iterate over all VCs returned by the fallback routing function
		for (int layer = get<1>(valid_hop_rf); layer <= get<2>(valid_hop_rf); layer++) {
			valid_hops.push_back(make_pair(nxt_rid, layer));
		}
	}
	return valid_hops;
}

// Valid hops are returned in the format [(next_router_id, layer), ...].
vector<pair<int,int>> lash_pat_routing::get_valid_hops_using_cdg(vector<map<int,map<int,tuple<int,int,int>>>> router_list, CDG_E* cdg, int n_routers, int n_nodes, int n_layers, int src_rid, vector<tuple<int,int,int>> prv_hops, int cur_rid, int in_layer, int dst_nid, int dst_rid, int in_port) const {
	// We compute all valid hops
	vector<pair<int,int>> valid_hops;
	// Construct a list of preivous routers and one of induced CDG edges
	// prv_hops has the form [(from_rid_1, to_rid_1, layer_1), ...]
	set<int> routers_on_current_paths;
	vector<pair<tuple<int,int,int>, tuple<int,int,int>>> temporary_cdg_edges;
	if (prv_hops.size() > 0) {
		routers_on_current_paths.insert(get<0>(prv_hops[0]));
		for (int i = 0; i < static_cast<int>(prv_hops.size()); i++) {
			routers_on_current_paths.insert(get<1>(prv_hops[i]));
			if (i > 0) {
				if (!cdg->has_edge(prv_hops[i-1], prv_hops[i])) {
					temporary_cdg_edges.push_back(make_pair(prv_hops[i-1], prv_hops[i]));
				}
			}
		}
	}
	// Insert edges on current path into the CDG
	cdg->insert_edges(temporary_cdg_edges);
	// Check all potential next hops for cycles
	for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
		int nxt_rid = it->first;
		// Exclude all routers on current paths. This avoids 180 degree turns and other loops which make no sense since we don't want to route in circles.
		if (routers_on_current_paths.find(nxt_rid) != routers_on_current_paths.end()) {
			continue; 
		}
		// Iterate over all VCs for the hop from cur_rid to nxt_rid.
		for (int out_layer = 0; out_layer < n_layers; out_layer++) {
			// If this hop is already in the routing table, then skip it, we only look for new hops.
			if (_routing_table[cur_rid][dst_rid].find(make_pair(in_port, in_layer)) != _routing_table[cur_rid][dst_rid].end()) {
				bool hop_already_in_routing_table = false;
				for (tuple<int,int,int,int> existing_hop : _routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)]) {
					if (get<3>(existing_hop) == nxt_rid && get<1>(existing_hop) == out_layer) {
						hop_already_in_routing_table = true;
						break;
					}
				}
				if (hop_already_in_routing_table) {
					continue;
				}
			}
			// If there is no previous router, then we can't close cycles in the CDG -> all next hops are valid.
			if (prv_hops.size() == 0) {
				valid_hops.push_back(make_pair(nxt_rid, out_layer));
			// If there is a previous router, we need to check if the hop from cur_rid to nxt_rid would close a cycle in the CDG. If it does, we cannot use this hop.
			} else {
				if (!cdg->creates_cycle(prv_hops.back(), make_tuple(cur_rid, nxt_rid, out_layer))) {
					valid_hops.push_back(make_pair(nxt_rid, out_layer));
				}
			}
		}
	}
	// Remove the edges on the current path from the CDG again to restore it to its original state for the next call of this function.
	cdg->remove_edges(temporary_cdg_edges);
	return valid_hops;
}

// Initialization which is only called once per simulation and which computes all routing tables.
void lash_pat_routing::initialize(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers) {
	cout << "Initializing LASH-PAT routing..." << endl;
	// Pre-computations: Identify all routers that are connected to nodes and map routers and ports to next router.
	_router_and_inport_to_prev_router.clear();
	_router_and_inport_to_prev_router.resize(n_routers);
	for (int rid = 0; rid < n_routers; rid++) {
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][rid].begin(); it != router_list[1][rid].end(); ++it) {
			_router_and_inport_to_prev_router[it->first][get<1>(it->second)] = rid;
		}
	}

	// Step 1: Configure a deterministic base routing with one path per source-destination pair.
	CDG_E* cdg;
	// Step 1a) Try to use our custom base routing. This writes the routing table and returns the corresponding CDG.
	cout << "- Configuring deterministic base routing using custom CDG-based routing function..." << endl;
	std::pair<bool, CDG_E*> success_and_cdg = find_and_commit_paths_base_routing(router_list, n_routers, n_nodes, n_layers, &lash_pat_routing::get_valid_hops_using_cdg);
	if (success_and_cdg.first) {
		cdg = success_and_cdg.second;
		_additional_info = 1;
		cout << "- Successfully configured deterministic base routing using custom CDG-based routing function." << endl;
	} else {
		// Step 1b.1) If custom base routing is not successful, use an external base routing function.
		cout << "- Failed to configure deterministic base routing using custom CDG-based routing function. Falling back to external base routing function..." << endl;
		clear_routing_table_and_stats();
		// This function will again write the routing table, but this time using the external base routing function. The CDG is returned for later use.
		success_and_cdg = find_and_commit_paths_base_routing(router_list, n_routers, n_nodes, n_layers, &lash_pat_routing::get_valid_hops_using_fallback_routing);
		if (success_and_cdg.first) {
			cout << "- Successfully configured deterministic base routing using external base routing function." << endl;
			cdg = success_and_cdg.second;
			_additional_info = 2;
		} else {
			cout << "Error in lash_pat_routing::initialize: Failed to configure base routing using both custom and external routing functions. Aborting." << endl;
			exit(1);
		}
		// Step 1b.2): We try to improve the deterministic base routing. If we used our custom routing, then this should not change anything,
		// 		   but if we had to fall back to the external routing, then this can already improve the routing tables and this ensures shortest-paths only and thereby 
		// 		   cycle-freedom (otherwise the combination of non-shortest path in base routing and shortest paths in multipath phase could create cycles.
		bool did_improve = true;
		int iteration = 0;
		while (did_improve) {
			iteration++;
			did_improve = improve_paths(cdg, router_list, n_routers, n_nodes, n_layers, &lash_pat_routing::get_valid_hops_using_cdg);
			cout << "- Iteration " << iteration << " of improving paths: " << (did_improve ? "Found better paths and improved routing tables." : "No better paths found, finished improving paths.") << endl;
		}
	}
	// We switch to multipath mode 
	_is_in_multipath_phase = true;

	// Step 2: We proceed with the multipath phase where we add additional paths to the routing tables.
	for (int i = 0; i < _n_additional_paths; i++) {
		cout << "- Configuring additional paths (iteration " << i+1 << " of " << _n_additional_paths << ")..." << endl;
		// In the multipath phase, success is true if we were able to add at least one additional path.
		bool success = find_and_commit_paths_multipath_routing(cdg, router_list, n_routers, n_nodes, n_layers, &lash_pat_routing::get_valid_hops_using_cdg);
		if (success) {
			cout << "- Successfully configured additional paths (iteration " << i+1 << " of " << _n_additional_paths << ")." << endl;
		} else {
			cout << "- Unable to configure additional paths (iteration " << i+1 << " of " << _n_additional_paths << "). No more paths can be added without creating cycles in the CDG. Finishing initialization." << endl;
			break;
		}
	}
	cout << "Finished initializing LASH-PAT routing." << endl;
}

// Writing a given hop into the routing table and adding it to the CDG.
void lash_pat_routing::commit_hop(CDG_E* cdg, int dst_rid, int prv_rid, int cur_rid, int cur_in_port, int cur_in_layer, int cur_out_port, int cur_out_layer, int nxt_rid, int path_length, double flow) {
	// If the previous router ID is -1, we are at the source, hence, the current hop cannot cause a cycle in the CDG and we can directly commit it.
	// Otherwise, we need to insert the corresponding edge into the CDG.
	if (prv_rid != -1) {
		tuple<int,int,int> from = make_tuple(prv_rid, cur_rid, cur_in_layer); 	// (from_rid, to_rid, layer)
		tuple<int,int,int> to = make_tuple(cur_rid, nxt_rid, cur_out_layer); 	// (from_rid, to_rid, layer)	
		// Add the hop to the CDG. This function internally checks that the CDG remains acyclic and throw an error if the hop creates a cycle.
		cdg->insert_edge(from, to);
	}
	// Configure the hop in the routing table. 
	// Format: routing_table[cur_rid][dst_rid][(in_port,in_layer)]= [(out_port, out_layer, path_length, next_rid), ...]
	_routing_table[cur_rid][dst_rid][make_pair(cur_in_port, cur_in_layer)].push_back(make_tuple(cur_out_port, cur_out_layer, path_length, nxt_rid));
	// Update the stats
	if (_paths_per_physical_channel.find(make_pair(cur_rid, nxt_rid)) == _paths_per_physical_channel.end()) {
		_paths_per_physical_channel[make_pair(cur_rid, nxt_rid)] = 0.0;
	}
	_paths_per_physical_channel[make_pair(cur_rid, nxt_rid)] += flow;
	if (_paths_per_virtual_channel.find(make_tuple(cur_rid, nxt_rid, cur_out_layer)) == _paths_per_virtual_channel.end()) {
		_paths_per_virtual_channel[make_tuple(cur_rid, nxt_rid, cur_out_layer)] = 0.0;
	}
	_paths_per_virtual_channel[make_tuple(cur_rid, nxt_rid, cur_out_layer)] += flow;
	if (_paths_per_virtual_channel_per_dst.find(make_tuple(cur_rid, nxt_rid, cur_out_layer, dst_rid)) == _paths_per_virtual_channel_per_dst.end()) {
		_paths_per_virtual_channel_per_dst[make_tuple(cur_rid, nxt_rid, cur_out_layer, dst_rid)] = 0.0;
	}
	_paths_per_virtual_channel_per_dst[make_tuple(cur_rid, nxt_rid, cur_out_layer, dst_rid)] += flow;
}

// Removes a hop from the CDG.
void lash_pat_routing::remove_hop(CDG_E* cdg, int dst_rid, int prv_rid, int cur_rid, int cur_in_port, int cur_in_layer, int cur_out_port, int cur_out_layer, int nxt_rid) {
	// If the previous router ID is -1, we are at the source, hence, there is on edge in the CDG for the current hop, so we can directly remove it from the routing table.
	// Otherwise, we need to remove the corresponding edge from the CDG.
	if (prv_rid != -1) {
		tuple<int,int,int> from = make_tuple(prv_rid, cur_rid, cur_in_layer); 	// (from_rid, to_rid, layer)
		tuple<int,int,int> to = make_tuple(cur_rid, nxt_rid, cur_out_layer); 	// (from_rid, to_rid, layer)	
		// Before removing the hop from the CDG, we need to make sure that this hop is not used for any other dst_rid
		// We do this by checking the routing table for all destinations other than dst_rid
		bool hop_still_needed_for_other_destination = false;
		for (int tmp_dst_rid : _routers_with_nodes_1) {
			if (tmp_dst_rid == dst_rid) {
				continue;
			}
			for (map<pair<int,int>, vector<tuple<int,int,int,int>>>::iterator it = _routing_table[cur_rid][tmp_dst_rid].begin(); it != _routing_table[cur_rid][tmp_dst_rid].end(); ++it) {
				if (it->first.first == cur_in_port && it->first.second == cur_in_layer) {
					for (tuple<int,int,int,int> hop: it->second) {
						if (get<0>(hop) == cur_out_port && get<1>(hop) == cur_out_layer && get<3>(hop) == nxt_rid) {
							hop_still_needed_for_other_destination = true;
							break;
						}
					}
					if (hop_still_needed_for_other_destination) {
						break;
					}
				}
			}
			if (hop_still_needed_for_other_destination) {
				break;
			}
		}
		// Remove the hop from the CDG. This function internally checks that the edge exists in the CDG before removing it.
		if (!hop_still_needed_for_other_destination) {
			cdg->remove_edge(from, to);
		}
	}
	// Remove the hop from the routing table.
	// Format: routing_table[cur_rid][dst_rid][(in_port,in_layer)]= [(out_port, out_layer, path_length, next_rid), ...]
	if (_routing_table[cur_rid][dst_rid].find(make_pair(cur_in_port, cur_in_layer)) == _routing_table[cur_rid][dst_rid].end()) {
		cout << "Error in lash_pat_routing::remove_hop: Trying to remove a hop from cur_rid = " << cur_rid << " to nxt_rid = " << nxt_rid << " for in_port = " << cur_in_port << " and in_layer = " << cur_in_layer << ", but there is no such hop in the routing table." << endl;
		exit(1);
	}
	bool hop_found = false;
	for (vector<tuple<int,int,int,int>>::iterator it = _routing_table[cur_rid][dst_rid][make_pair(cur_in_port, cur_in_layer)].begin(); it != _routing_table[cur_rid][dst_rid][make_pair(cur_in_port, cur_in_layer)].end(); ++it) {
		if (get<3>(*it) == nxt_rid && get<1>(*it) == cur_out_layer) {
			_routing_table[cur_rid][dst_rid][make_pair(cur_in_port, cur_in_layer)].erase(it);
			// If this routing able entry is now empty, we remove it from the routing table to keep it clean.
			if (_routing_table[cur_rid][dst_rid][make_pair(cur_in_port, cur_in_layer)].size() == 0) {
				_routing_table[cur_rid][dst_rid].erase(make_pair(cur_in_port, cur_in_layer));
			}
			hop_found = true;
			break;
		}
	}
	if (!hop_found) {
		cout << "Error in lash_pat_routing::remove_hop: Trying to remove a hop from cur_rid = " << cur_rid << " to nxt_rid = " << nxt_rid << " for in_port = " << cur_in_port << " and in_layer = " << cur_in_layer << ", but there is no such hop in the routing table." << endl;
		exit(1);
	}
}


// Find and commits one deterministic path for each source-destination pair using the provided get_valid_hops function.
pair<bool,CDG_E*> lash_pat_routing::find_and_commit_paths_base_routing(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops) {
	CDG_E* cdg = new CDG_E(router_list, n_routers, n_layers);
	int prv_rid = -1; // In the base routing, the source router is the actual source of the path, hence, there is no previous hop and we can set prv_rid to -1.
	for (int src_rid : _routers_with_nodes_1) {
		cout << "-- Configuring all base paths from source router " << src_rid << "..." << endl;
		for (int dst_rid : _routers_with_nodes_2) {
			// Skip if source and destination are the same
			if (src_rid == dst_rid) {
				continue;
			}
			// Find and commit paths from src_rid to dst_rid using the provided routing function. This will also update the CDG.
			bool success = find_and_commit_paths(cdg, router_list, n_routers, n_nodes, n_layers, get_valid_hops, prv_rid, src_rid, dst_rid, -1, 0, 1.0, numeric_limits<double>::infinity());
			if (!success) {
				delete cdg;
				return make_pair(false, nullptr);
			}
		}
	}
	return make_pair(true, cdg);
}

// Replace existing paths with better paths.
bool lash_pat_routing::improve_paths(CDG_E* cdg, vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops) {
	// Variables used throughout function
	int n_paths, prv_rid, in_port, in_layer, tmp_cur_rid, tmp_prv_rid, tmp_in_port, tmp_in_layer, tmp_out_port, tmp_out_layer, tmp_nxt_rid;
	double max_path_cost;
	bool better_path_found, at_least_one_path_found = false, does_converge;
	for (int cur_rid : _routers_with_nodes_1) {
		cout << "-- Trying to improve paths from source router " << cur_rid << "..." << endl;
		for (int dst_rid : _routers_with_nodes_2) {
			// Skip if source and destination are the same
			if (cur_rid == dst_rid) {
				continue;
			}
			// We need to firs collect all existing paths from cur_rid to dst_rid since we will modify the routing table and the CDG while iterating over the existing paths, which would cause problems if we directly iterate over the routing table.
			// Format: [(in_port, in_layer), [(out_port, out_layer, path_length, next_rid)], ...]
			vector<pair<pair<int,int>, vector<tuple<int,int,int,int>>>> entries_to_process;
			for (map<pair<int,int>, vector<tuple<int,int,int,int>>>::iterator it = _routing_table[cur_rid][dst_rid].begin(); it != _routing_table[cur_rid][dst_rid].end(); ++it) {
				entries_to_process.push_back(make_pair(it->first, it->second));
			}
			for (pair<pair<int,int>, vector<tuple<int,int,int,int>>> entry : entries_to_process) {
				in_port = entry.first.first;
				in_layer = entry.first.second;
				// Here, we set to max_path_cost to the cost of the existing path.
				// Since this is an exclusive upper limit, the new path must be strictly shorter than the existing one in order to be committed.
				max_path_cost = floor(get<2>(entry.second[0]));
				// We set the prev router id to -1 if we are at injection, otherwise we set it to the actual previous router id.
				prv_rid = -1;
				if (_router_and_inport_to_prev_router[cur_rid].find(in_port) != _router_and_inport_to_prev_router[cur_rid].end()) {
					prv_rid = _router_and_inport_to_prev_router[cur_rid][in_port];
				}
				// We expect that this function is called on the deterministic base routing, hence, we do not expect multiple paths.
				n_paths = entry.second.size();
				if (n_paths > 1) {
					cout << "Error in lash_pat_routing::improve_paths: Found multiple existing paths from cur_rid = " << cur_rid << " to dst_rid = " << dst_rid << " for in_port = " << in_port << " and in_layer = " << in_layer << "." << endl; 
					cout << "This function is supposed to be called on the deterministic base routing where we expect only one path per source-destination pair. There are currently " << n_paths << " paths." << endl;
					exit(1);
				}
				// Identify the incoming flow for which we find a better path
				int flow;
				if (prv_rid == -1) {
					flow = 1.0; // Since we are at injection, we have flow = 1.0
				} else {
					flow = _paths_per_virtual_channel_per_dst[make_tuple(prv_rid, cur_rid, in_layer, dst_rid)];
				}
				// Remove the flow from the existing path: This path should never diverge since we are still on the deterministic base routing.
				propagate_channel_utilization_change(router_list, in_port, in_layer, cur_rid, dst_rid, -1.0 * flow);
				// Find a better path; If such a path is found, it is directly committed to the routing table and the CDG inside the find_and_commit_paths function. The flow is set accordingly.
				better_path_found = find_and_commit_paths(cdg, router_list, n_routers, n_nodes, n_layers, get_valid_hops, prv_rid, cur_rid, dst_rid, in_port, in_layer, flow, max_path_cost);
				// If we found a better path, we need to remove the old, worse path from both the routing table and the CDG.
				if (better_path_found) {
					at_least_one_path_found = true;
					// Step 1: We follow the path until the old and new path diverge
					tmp_prv_rid = prv_rid;
					tmp_cur_rid = cur_rid;
					tmp_in_port = in_port;
					tmp_in_layer = in_layer;
					tmp_out_port = get<0>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
					tmp_out_layer = get<1>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
					tmp_nxt_rid = get<3>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
					pair<int,int> tmp_in = make_pair(tmp_in_port, tmp_in_layer);
					does_converge = false;
					while (_routing_table[tmp_cur_rid][dst_rid].find(tmp_in) != _routing_table[tmp_cur_rid][dst_rid].end() && _routing_table[tmp_cur_rid][dst_rid][tmp_in].size() == 1) {
						// Check if there are any other paths that converge in the current router and set does_converge accordingly before proceeding to the next router.
						does_converge = false;
						for (map<pair<int,int>, vector<tuple<int,int,int,int>>>::iterator it = _routing_table[tmp_cur_rid][dst_rid].begin(); it != _routing_table[tmp_cur_rid][dst_rid].end(); ++it) {
							for (tuple<int,int,int,int> hop: it->second) {
								if (get<0>(hop) == tmp_out_port && get<1>(hop) == tmp_out_layer) {
									does_converge = true;
									break;
								}
							}
							if (does_converge) {
								break;
							}
						}
						tmp_prv_rid = tmp_cur_rid;
						tmp_cur_rid = tmp_nxt_rid;
						tmp_in_port = get<1>(router_list[1][tmp_prv_rid][tmp_cur_rid]);
						tmp_in_layer = tmp_out_layer;
						// Sanity check: If we reached the destination, something went wrong since the paths should have diverged at some point. Aborting.
						if (tmp_cur_rid == dst_rid) {
							cout << "Error in lash_pat_routing::improve_paths: Reached the destination router while following the old path from cur_rid = " << cur_rid << " to dst_rid = " << dst_rid << " for in_port = " << in_port << " and in_layer = " << in_layer << "." << endl;
							cout << "This should not happen since the new path should diverge from the old path at some point. Aborting." << endl;
							exit(1);
						}
						tmp_out_port = get<0>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
						tmp_out_layer = get<1>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
						tmp_nxt_rid = get<3>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
						tmp_in = make_pair(tmp_in_port, tmp_in_layer);
						// Check if two paths converge at the 
					}
					// Sanity check: Now there are two paths in the routing table
					if (_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)].size() != 2) {
						cout << "Error in lash_pat_routing::improve_paths: After finding a better path from cur_rid = " << cur_rid << " to dst_rid = " << dst_rid << " for in_port = " << in_port << " and in_layer = " << in_layer << ", ";
						cout << "there should be exactly two paths in the routing table at the router where the new and old path diverge, since we are still following the old path." << endl;
						exit(1);
					}
					// Sanity check:The new path should be shorter than the old one
					if (get<2>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]) <= get<2>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][1])) {
						cout << "Error in lash_pat_routing::improve_paths: The new path from cur_rid = " << cur_rid << " to dst_rid = " << dst_rid << " for in_port = " << in_port << " and in_layer = " << in_layer;
					   	cout <<	" should be shorter than the old path, since we set the max_path_cost to the length of the old path. However, this is not the case. Aborting." << endl;
						cout << "New path length: " << get<2>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]) << ", Old path length: " << get<2>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][1]) << endl;
						exit(1);
					}
					// Step 3: We keep following he old path and removing it from routing tables as long as it does not converge with any other path.
					// Check, if we have two converging paths at the prv_rid
					// The correct value for does_converge is set above when searching for the divergence point.
					// In case the divergence point is the same as the convergence point, we do not want to remove any hops.
					while (!does_converge && tmp_cur_rid != dst_rid) {
						remove_hop(cdg, dst_rid, tmp_prv_rid, tmp_cur_rid, tmp_in_port, tmp_in_layer, tmp_out_port, tmp_out_layer, tmp_nxt_rid);
						// Check if the current router hay any other paths that use the same out-port and out-VC. If yes, we stop, otherwise, we keep following the path.
						does_converge = false;
						for (map<pair<int,int>, vector<tuple<int,int,int,int>>>::iterator it = _routing_table[tmp_cur_rid][dst_rid].begin(); it != _routing_table[tmp_cur_rid][dst_rid].end(); ++it) {
							for (tuple<int,int,int,int> hop: it->second) {
								if (get<0>(hop) == tmp_out_port && get<1>(hop) == tmp_out_layer) {
									does_converge = true;
									break;
								}
							}
							if (does_converge) {
								break;
							}
						}
						// If it does not converge and we are not at the destination, we keep following the path. Otherwise, we stop.
						if (!does_converge && tmp_nxt_rid != dst_rid) {
							tmp_prv_rid = tmp_cur_rid;
							tmp_cur_rid = tmp_nxt_rid;
							tmp_in_port = get<1>(router_list[1][tmp_prv_rid][tmp_cur_rid]);
							tmp_in_layer = tmp_out_layer;
							// Sanity check: There should be exactly one path in the routing table for the current hop that we are removing, since we are still on the old path.
							if (_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)].size() != 1) {
								cout << "Error in lash_pat_routing::improve_paths: After removing the old path from cur_rid = " << tmp_cur_rid << " to dst_rid = " << dst_rid << " for in_port = " << tmp_in_port << " and in_layer = " << tmp_in_layer;
								cout << ", there should be exactly one next hop in the routing table, since we are still on the old deterministic base path." << endl;
								cout << "However, there are currently " << _routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)].size() << " paths. Aborting." << endl;
								exit(1);
							}
							tmp_nxt_rid = get<3>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
							tmp_out_port = get<0>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
							tmp_out_layer = get<1>(_routing_table[tmp_cur_rid][dst_rid][make_pair(tmp_in_port, tmp_in_layer)][0]);
						// We either reached the convergence point or the destination, so we stop.
						} else {
							break;
						}
					}
					// If the above while-loop terminated, we removed all hops along the old path that are not needed by some other path that converges with our old path.
				} else {
					// If we did not find a better path, nothing was committed and we need to add back the flow to the existing path in the stats.
					propagate_channel_utilization_change(router_list, in_port, in_layer, cur_rid, dst_rid, flow);
				}
			}
		}
	}
	return at_least_one_path_found;
}

// Find and commit additional paths for source-destination pairs using the provided get_valid_hops function.
bool lash_pat_routing::find_and_commit_paths_multipath_routing(CDG_E* cdg, vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops) {
	bool at_lest_one_path_found = false;
	for (int cur_rid : _routers_with_nodes_1) {
		cout << "-- Configuring additional paths from source router " << cur_rid << "..." << endl;
		for (int dst_rid : _routers_with_nodes_2) {
			// Skip if source and destination are the same
			if (cur_rid == dst_rid) {
				continue;
			}
			for (map<pair<int,int>, vector<tuple<int,int,int,int>>>::iterator it = _routing_table[cur_rid][dst_rid].begin(); it != _routing_table[cur_rid][dst_rid].end(); ++it) {
				int in_port = it->first.first;
				int in_layer = it->first.second;
				// Compute the max cost: 1 + floor(cost). The actual cost must be strictly lower than this max path cost,
				// which results in equal length but potentially higher link utilization along the path.
				double max_path_cost = 1 + floor(get<2>(it->second[0]));
				// We set the prev router id to -1 if we are at injection, otherwise we set it to the actual previous router id.
				int prv_rid = -1;
				if (_router_and_inport_to_prev_router[cur_rid].find(in_port) != _router_and_inport_to_prev_router[cur_rid].end()) {
					prv_rid = _router_and_inport_to_prev_router[cur_rid][in_port];
				}
				// Identify the incoming flow for which we find an additional path.
				double flow;
				if (prv_rid == -1) {
					flow = 1.0; // Since we are at injection, we have flow = 1.0
				} else {
					flow = _paths_per_virtual_channel_per_dst[make_tuple(prv_rid, cur_rid, in_layer, dst_rid)];
				}
				// Identify the number of outgoing paths from cur_rid to dst_rid for the current in_port and in_layer. This is needed to set the flow for the new path that we want to add.
				int n_outgoing_paths = it->second.size();
				int flow_of_new_path = flow / (n_outgoing_paths + 1);
				// Reduce flow along all existing paths
				propagate_channel_utilization_change(router_list, in_port, in_layer, cur_rid, dst_rid, -1.0 * flow_of_new_path);
				// Try to find a new path and add its flow
				bool path_found = find_and_commit_paths(cdg, router_list, n_routers, n_nodes, n_layers, get_valid_hops, prv_rid, cur_rid, dst_rid, in_port, in_layer, flow_of_new_path, max_path_cost);
				at_lest_one_path_found |= path_found;
				// If no new path was found, add back the flow to the existing paths in the stats.
				if (!path_found) {
					propagate_channel_utilization_change(router_list, in_port, in_layer, cur_rid, dst_rid, flow_of_new_path);
				}
			}
		}
	}
	return at_lest_one_path_found;
}

// This function finds a path from src_rid to dst_rid using the provided get_valid_hops function and commits it to the routing table and CDG if it is valid.
// It returns true if a path was found and committed successfully, and false otherwise. 
// A path is valid if it does not create cycles in the CDG and if its cost is below the provided max_path_cost.
// NOTE: Cost functions must ensure that floor(cost) = distance.
bool lash_pat_routing::find_and_commit_paths(CDG_E* cdg, vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops, int ext_prv_rid, int src_rid, int dst_rid, int in_port, int in_layer, double flow, double max_path_cost) {
	// Variables used throughout function
	int dst_nid;													// Node IDs
	int cur_in_port, cur_out_port;									// Ports 
	int cur_in_layer, cur_out_layer;										// VCs
	int cur_rid, nxt_rid, prv_rid;									// Router IDs
	double cur_cost, nxt_cost, tot_cost;								// Cost for Dijkstra (Distance from source or a more complex cost function)
	tuple<int,int,int> cur_vertex, nxt_vertex;						// Vertices for Dijkstra
	vector<pair<int,int>> valid_next_hops;							// Valid next hops from routing function
	// Routing functions need nid not only a rid
	dst_nid = router_list[0][dst_rid].begin()->first;
	// Variable to track how destination was reached, needed for backtracking
	int terminal_rid = -1, in_port_at_terminal = -1, in_layer_at_terminal = -1;
	bool terminal_is_over_existing_path = false; // This variable is needed to decide whether we need to update the path count stats along the path from the terminal router to the destination router after we committed the new path.
	double path_cost_over_terminal = numeric_limits<double>::infinity();
	// Perform Dijkstra to find a path from src_rid to dst_rid.
	// Since fallback routing function is deadlock-free, we do not need any checks during Dijkstra.
	// However, we build and check the CDG when we commit the paths as a double check.
	// For our Dijkstra, the "vertices" are (cur_rid, in_port, in_layer) tuples since the valid next hops depend on these three parameters.
	std::map<tuple<int,int,int>, tuple<int,int,int>> parent; 	// Map
	std::map<tuple<int,int,int>, double> cost; 					// Map from vertex to cost
	std::set<tuple<int,int,int>> visited;						// Set jto track visited vertices
	// Start Dijkstra from the source vertex (src_rid, in_port, in_layer)
	// ext_prv_rid is -1 if src_rid is the real source (base or multipath) and the actual ext_prv_rid if src_rid is not the real source but an intermediate hop for which we want to add more paths.
	parent[make_tuple(src_rid, in_port, in_layer)] = make_tuple(ext_prv_rid, -1, -1); 
	cost[make_tuple(src_rid, in_port, in_layer)] = 0.0;			// This is the cost to reach a given vertex from the source
	// Create a priority queue which contains both the vertices and their cost. The priority queue is ordered by cost.
	priority_queue<pair<double, tuple<int,int,int>>, vector<pair<double, tuple<int,int,int>>>, greater<pair<double, tuple<int,int,int>>>> queue;
	queue.push(make_pair(0.0, make_tuple(src_rid, in_port, in_layer)));
	// Main Dijkstra loop
	while (!queue.empty()) {
		// Get vertex with smallest cost
		cur_cost = queue.top().first;
		cur_vertex = queue.top().second;
		queue.pop();
		cur_rid = get<0>(cur_vertex);
		in_port = get<1>(cur_vertex);
		in_layer = get<2>(cur_vertex);
		// Skip if we already found a better path to this vertex
		if (cost.find(cur_vertex) != cost.end()) {
			if (cost[cur_vertex] < cur_cost) {
				continue;
			}
		}
		// Error if we visit the same vertex twice
		if (visited.find(cur_vertex) != visited.end()) {
			cout << "Error in lash_pat_routing::find_and_commit_paths: Trying to visit the vertex (cur_rid = " << cur_rid << ", in_port = " << in_port << ", in_layer = " << in_layer << ") twice. Aborting." << endl;
			exit(1);
		}
		visited.insert(cur_vertex);
		// If we reached the destination router, we can stop Dijkstra
		// Make sure to store the in_port and in_layer at the destination router that corresponds to the path we found
		// This data is needed for backtracking from destination to source to write the routing table and set the CDG.
		if (cur_rid == dst_rid) {
			// cur_cost is cost to reach dst_rid from src_rid.
			// path_cost_over_terminal is cost to reach dst_rid from src_rid if we only route to terminal_rid and use existing path from there.
			// If we compare this path which reaches the destination to one that uses an existing path, we need to subtract the existing path bias from the currently store path.
			if (cur_cost < (path_cost_over_terminal - (terminal_is_over_existing_path ? _existing_path_bias : 0))) {
				terminal_rid = dst_rid;
				in_port_at_terminal = in_port;
				in_layer_at_terminal = in_layer;
				path_cost_over_terminal = cur_cost;
			}
			break;
		}
		// Identify all previous hops leading to the current router.
		vector<tuple<int,int,int>> prv_hops;							// Given as (from_rid, to_rid, layer)
		tuple<int,int,int> cur_prv_vertex = cur_vertex;					// Given as (rid, in_port, in_layer)
		tuple<int,int,int> prv_prv_vertex = parent[cur_prv_vertex];		// Given as (rid, in_port, in_layer)
		while (get<1>(cur_prv_vertex) != -1) {
			prv_hops.push_back(make_tuple(get<0>(prv_prv_vertex), get<0>(cur_prv_vertex), get<2>(cur_prv_vertex)));
			cur_prv_vertex = prv_prv_vertex;
			prv_prv_vertex = parent[cur_prv_vertex];
		}
		reverse(prv_hops.begin(), prv_hops.end());
		// Get valid next hops from routing function.
		valid_next_hops = (this->*get_valid_hops)(router_list, cdg, n_routers, n_nodes, n_layers, src_rid, prv_hops, cur_rid, in_layer, dst_nid, dst_rid, in_port);
		// Iterate through hops
		for (pair<int,int> next_hop : valid_next_hops) {
			nxt_rid = next_hop.first;
			int nxt_in_port = get<1>(router_list[1][cur_rid][nxt_rid]); 	// Get input port at next router that corresponds to current router
			int nxt_in_layer = next_hop.second;
			nxt_vertex = make_tuple(nxt_rid, nxt_in_port, nxt_in_layer);
			// Set cost_function_base or cost_function_multipath depending on the phase we are in.
			double (lash_pat_routing::*cost_function)(int, int, int, int) const;
			string cost_function_type;
			if (_is_in_multipath_phase) {
				cost_function = _cost_function_multipath;
				cost_function_type = _cost_function_multipath_type;
			} else {
				cost_function = _cost_function_base;
				cost_function_type = _cost_function_base_type;
			}
			double hop_cost = (this->*cost_function)(cur_rid, nxt_rid, nxt_in_layer, n_routers);
			// Special case for the cost function "length_and_cdg_edge"
			if (cost_function_type == "cdg" or cost_function_type == "all" or cost_function_type == "red_all") {
				if (parent.find(cur_vertex) != parent.end()) {
					tuple<int,int,int> parent_vertex = parent[cur_vertex];
					if (get<0>(parent_vertex) != -1) {
						bool has_cdg_edge = cdg->has_edge(make_tuple(get<0>(parent_vertex), cur_rid, in_layer), make_tuple(cur_rid, nxt_rid, nxt_in_layer));
						hop_cost += (has_cdg_edge ? 0 : 1.0 / n_routers); // Penalty of needing to create a new CDG edge, sums up to <1.0 along the whole path.
					}
				}
			}
			// Special case only for the "all" cost function: We try to promote paths that use a different physical channel than the existing paths
			if (cost_function_type == "all") {
				if (_routing_table[cur_rid][dst_rid].find(make_pair(in_port, in_layer)) != _routing_table[cur_rid][dst_rid].end()) {
					bool uses_different_physical_channel = true;
					for (tuple<int,int,int,int> hop : _routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)]) {
						if (nxt_rid == get<3>(hop)) {
							uses_different_physical_channel = false;
							break;
						}
					}
					hop_cost += (uses_different_physical_channel ? 0.0 : 1.0 / n_routers);
				}
			}
			// Aggregate cost along the path depending on the cost aggregation function.
			if (_cost_aggregation == "sum") {
				nxt_cost = cur_cost + hop_cost;
			} else if (_cost_aggregation == "max") {
				double cur_frac = cur_cost - floor(cur_cost);
				double hop_frac = hop_cost - floor(hop_cost);
				nxt_cost = floor(cur_cost) + floor(hop_cost) + max(cur_frac, hop_frac);
			} else {
				cout << "Error in lash_pat_routing::find_and_commit_paths: Invalid cost aggregation function: " << _cost_aggregation << ". Aborting." << endl;
				exit(1);
			}
			// If this neighbor already has a shortest path configured to the destination
			if (_routing_table[nxt_rid][dst_rid].find(make_pair(nxt_in_port, nxt_in_layer)) != _routing_table[nxt_rid][dst_rid].end()) {
				// nxt_cost is cost to reach nxt_rid from src_rid
				// get<2>(_routing_table[nxt_rid][dst_rid][make_pair(in_port, in_layer)][0]) is distance from nxt_rid to dst_rid along existing path
				// The existing_path_bias is only subtracted if we compare a new path that goes through an existing path with an old path that is not initialized or that does not go over an existing path.
				tot_cost = nxt_cost + get<2>(_routing_table[nxt_rid][dst_rid][make_pair(nxt_in_port, nxt_in_layer)][0]); // Routing table contains the path length, i.e. we ignore additional cost based on link utilization for the route that is already in the routing table.
				if (((tot_cost - (terminal_is_over_existing_path ? 0 : _existing_path_bias)) < path_cost_over_terminal)) {
					if (parent.find(nxt_vertex) != parent.end() and (visited.find(nxt_vertex) != visited.end())) {
						cout << "Error in lash_pat_routing::find_and_commit_paths: Found a path to the destination that is better than the existing path, but the next vertex on this path already has a parent." << endl;
						exit(1);
					} else {
						terminal_is_over_existing_path = true;
						parent[nxt_vertex] = cur_vertex;
						terminal_rid = nxt_rid;
						in_port_at_terminal = nxt_in_port;
						in_layer_at_terminal = nxt_in_layer;
						path_cost_over_terminal = tot_cost;
					}
				}
			}
			// If this neighbor does not have a shortest path configured to the destination yet
			else {
				// The next vertex has not been visited yet, add to queue
				if (cost.find(nxt_vertex) == cost.end()){
					cost[nxt_vertex] = nxt_cost;
					parent[nxt_vertex] = cur_vertex;
					queue.push(make_pair(nxt_cost, nxt_vertex));
				// If we found a better path to an unvisited vertex, update the cost and parent and add to queue with the new cost.
				} else if (nxt_cost < cost[nxt_vertex] && visited.find(nxt_vertex) == visited.end()) {
					cost[nxt_vertex] = nxt_cost;
					parent[nxt_vertex] = cur_vertex;
					queue.push(make_pair(nxt_cost, nxt_vertex));
				// With our version of Diijkstra, we should never find a better path to an already visited vertex.
				// Such updates might make the CDG cyclic since they change the dependencies of downstream paths.
				} else if (nxt_cost < cost[nxt_vertex]) {
					cout << "Error in lash_pat_routing::find_and_commit_paths: Found a better path to an already visited vertex. This should not happen with our version of Dijkstra. Aborting." << endl;
					exit(1);
				}			
			}
		}
	}
	// Verify that we found a path to the destination router.
	// This can fail in custom deterministic base routing or in the multipath phase, but in the fallback deterministic base routing, this should never happen.
	if (terminal_rid == -1 || in_port_at_terminal == -1 || in_layer_at_terminal == -1) {
		return false;
	}
	// If the path cost exceeds the max_path_cost, we consider this a failure.
	// This can happen in the multipath phase when we try to add additional paths that are significantly worse than the existing ones.
	if (path_cost_over_terminal >= max_path_cost) {
		return false;
	}
	// Backtrack from destination vertex to source vertex to get the path.
	// Along the way, write the routing table and build the CDG.
	nxt_vertex = make_tuple(terminal_rid, in_port_at_terminal, in_layer_at_terminal);
	int path_length;
	while (get<0>(nxt_vertex) != src_rid) {
		cur_vertex = parent[nxt_vertex];
		cur_rid = get<0>(cur_vertex);
		nxt_rid = get<0>(nxt_vertex);
		prv_rid = get<0>(parent[cur_vertex]);
		cur_in_port = get<1>(cur_vertex);
		cur_in_layer = get<2>(cur_vertex);
		cur_out_port = get<0>(router_list[1][cur_rid][nxt_rid]); 		// Get output port at current router that corresponds to next router
		cur_out_layer = get<2>(nxt_vertex); 								// Output VC at current router is the same as input VC at next router
		// path_cost_over_terminal is cost to reach dst_rid from src_rid along the path we found (either through a new path or through an existing path)
		// cost[cur_vertex] is cost to reach cur_rid from src_rid along the path we found
		// the path_cost is the cost to reach dst_rid from cur_rid along the path we found, which is needed for the routing table entry
		// Our invariant is that floor(cost) = path_length for all variables associated with cost and for all cost functions.
		path_length = static_cast<int>(floor(path_cost_over_terminal)) - static_cast<int>(floor(cost[cur_vertex])); // Works even with max cost because we still ensure that floor(cost) = path_length 
		// Commit hop to routing table and CDG
		commit_hop(cdg, dst_rid, prv_rid, cur_rid, cur_in_port, cur_in_layer, cur_out_port, cur_out_layer, nxt_rid, path_length, flow);
		// Move to next hop in path
		nxt_vertex = cur_vertex;
	}
	// We need to update the path count along the path from the terminal router to the destination router
	propagate_channel_utilization_change(router_list, in_port_at_terminal, in_layer_at_terminal, terminal_rid, dst_rid, flow);
	// If we did not abort before, then, we successfully found and committed paths for all source-destination pairs, so we can return true and the CDG.
	return true;
}

vector<tuple<int,int,int>> lash_pat_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	int in_layer = (in_vc * _n_layers) / n_vcs;
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this 
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "lash_pat_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + "_" + to_string(_n_layers) + "layers_" + to_string(_n_additional_paths) + "paths_" + _cost_function_base_type + "_" + _cost_function_multipath_type + "_epb" + to_string(_existing_path_bias) + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType1 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			_additional_info = rt._additional_info;
			cout << "- Loaded LASH-PAT routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<lash_pat_routing*>(this)->initialize(router_list, n_routers, n_nodes, _n_layers);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType1 rt;
				rt._routing_table = _routing_table;
				rt._additional_info = _additional_info;
				rt.save(rt_cache_file);
				cout << "Saved LASH-PAT routing tables to cache: " << rt_cache_file << endl;
			}
		}
		_is_initialized = true;
	}
	vector<tuple<int,int,int>> valid_next_hops;
	int output_port, output_layer, output_vc, layer_start, layer_end;
	// If we are already at the destination router, we can directly route to the destination node
	if (cur_rid == dst_rid) {	
		output_port = get<0>(router_list[0][cur_rid][dst_nid]);						// Identify output port to dst_nid
		valid_next_hops.push_back(make_tuple(output_port, 0, n_vcs - 1));			// We can us all VCs for ejection.
	} else {
		// If we are at the source (injection), we replace the real input-port with -1 to indicate that we are injecting.
		if (cur_rid == src_rid) {
			in_port = -1;		// Use -1 to indicate injection
			in_layer = 0; 		// Use VC 0 for injection
		}
		// Read next hops from routing table. Format is (output_port, output_layer, path_length, next_rid)
		vector<tuple<int,int,int,int>> next_hops = _routing_table[cur_rid][dst_rid][make_pair(in_port, in_layer)];
		for (tuple<int,int,int,int> next_hop : next_hops) {
			output_port = get<0>(next_hop);
			output_layer = get<1>(next_hop);
			layer_start = (output_layer * n_vcs) / _n_layers;
			layer_end = ((output_layer + 1) * n_vcs) / _n_layers;
			for (output_vc = layer_start; output_vc < layer_end; output_vc++) {
				valid_next_hops.push_back(make_tuple(output_port, output_vc, output_vc));
			}
		}
	}
	return valid_next_hops;
}

void lash_pat_routing::propagate_channel_utilization_change(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int in_port, int in_layer, int src_rid, int dst_rid, double flow_change) {

	struct State {
		int rid, in_port, in_layer;

		bool operator==(const State& o) const {
			return rid == o.rid && in_port == o.in_port && in_layer == o.in_layer;
		}
	};

	struct StateHash {
		size_t operator()(const State& s) const {
			return ((size_t)s.rid * 73856093) ^
				   ((size_t)s.in_port * 19349663) ^
				   ((size_t)s.in_layer * 83492791);
		}
	};

	unordered_map<State, double, StateHash> cur, next;
    cur[{src_rid, in_port, in_layer}] = flow_change;

    while (!cur.empty()) {
        next.clear();
        for (auto& [state, flow] : cur) {
            auto [cur_rid, cur_in_port, cur_in_layer] = state;
            if (cur_rid == dst_rid) {
                continue;
            }
            auto& next_hops = _routing_table[cur_rid][dst_rid][make_pair(cur_in_port, cur_in_layer)];

            if (next_hops.empty()) continue;

            double flow_per_path = flow / next_hops.size();

            for (auto& nh : next_hops) {
                int out_layer   = get<1>(nh);
                int nxt_rid  = get<3>(nh);

                // update utilization
                _paths_per_physical_channel[{cur_rid, nxt_rid}] += flow_per_path;
                _paths_per_virtual_channel[{cur_rid, nxt_rid, out_layer}] += flow_per_path;
                _paths_per_virtual_channel_per_dst[{cur_rid, nxt_rid, out_layer, dst_rid}] += flow_per_path;

                int nxt_in_port = get<1>(router_list[1][cur_rid][nxt_rid]);
                int nxt_in_layer   = out_layer;

                State nxt_state = {nxt_rid, nxt_in_port, nxt_in_layer};

                // MERGING happens here
                next[nxt_state] += flow_per_path;
            }
        }
        cur.swap(next);
    }
}


int lash_pat_routing::get_additional_info() const {
	return _additional_info;
}



// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> lash_pat_routing::get_max_routing_table_size(int N, int P, int V) {
	// Variables to be set based on routing table: C: Max number of routing choices per hop (over all cur_rids, dst_rids, (in-port, in-layer)-pairs), E: Max number of entries in the routing table (over all cur_rids)
	int C = 0, E = 0;
	// In LASH-PAT, the routing table size depends on the number of layers, not the number of VCs, hence, we overwrite V with _n_layers for the calculation of the routing table size.
	V = _n_layers;
	// Number of destinations equals number of routers with nodes
	for (map<int, map<int, map<pair<int,int>, vector<tuple<int,int,int,int>>>>>::const_iterator it = _routing_table.begin(); it != _routing_table.end(); ++it) {
		int E_tmp = 0;
		for (map<int, map<pair<int,int>, vector<tuple<int,int,int,int>>>>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
			for (map<pair<int,int>, vector<tuple<int,int,int,int>>>::const_iterator it3 = it2->second.begin(); it3 != it2->second.end(); ++it3) {
				E_tmp++;
				C = max(C, static_cast<int>(it3->second.size()));
			}
		}
		E = max(E, E_tmp);
	}
	// Compute table size based on variables identified above
	int dense_rows = N * (P * V + 1);
	int bits_list = C * (ceil(log2(P)) + ceil(log2(V)));
	int bits_bitmap = P * V;
	int csr_it_rows = N * (P * V + 1);
	int csr_it_bits = ceil(log2(E));
	int csr_rows = E;
	int dense_list = dense_rows * bits_list;
	int dense_bitmap = dense_rows * bits_bitmap;
	int csr_list = csr_it_rows * csr_it_bits + csr_rows * bits_list;
	int csr_bitmap = csr_it_rows * csr_it_bits + csr_rows * bits_bitmap;
	cout << "Max routing table size for LASH-PAT with N = " << N << ", P = " << P << ", V = " << V << " (with actual number of layers = " << _n_layers << "):" << endl;
	cout << "- Dense format: " << dense_list << " bits for list and " << dense_bitmap << " bits for bitmap." << endl;
	cout << "- CSR format: " << csr_list << " bits for list and " << csr_bitmap << " bits for bitmap." << endl;	
	return make_tuple(dense_list, dense_bitmap, csr_list, csr_bitmap);
}
