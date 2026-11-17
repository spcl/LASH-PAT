#include "routing_analyzer.hpp"
#include "routing_function.hpp"
#include "edge_based_cdg.hpp"
#include "statistics.hpp"
#include <iostream>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <tuple>
#include <climits>
#include <cmath>

using namespace std;

template<typename T>
map<T, int> create_histogram(const std::map<int, std::map<int, T>>& path_lengths) {
  std::map<T, int> histogram;  // key = value type (int/long/...)
  for (auto it = path_lengths.begin(); it != path_lengths.end(); ++it) {
    for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
      T path_length = it2->second;
      histogram[path_length]++;
    }
  }
  return histogram;
}

map<int,map<int,int>> compute_shortest_paths_wrt_topology(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, vector<int> routers_with_nodes){
	// Compute shortest paths with respect to the topology using BFS.
	map<int,map<int,int>> shortest_paths;
	for (int src_rid : routers_with_nodes){
		for (int dst_rid : routers_with_nodes){
			if (src_rid == dst_rid){
				shortest_paths[src_rid][dst_rid] = 0;  // Distance to itself is 0
				continue;
			}
			// BFS to find shortest path from src_rid to dst_rid with respect to the topology. 
			vector<bool> visited(n_routers, false);
			queue<pair<int,int>> q;  // Format: (cur_rid, dist)
			q.push(make_pair(src_rid, 0));
			visited[src_rid] = true;
			bool path_found = false;
			while (!q.empty() && !path_found){
				int cur_rid = q.front().first;
				int cur_dist = q.front().second;
				q.pop();
				// Get neighbors of the current router
				for (map<int, tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it){
					int neighbor_rid = it->first;
					if (!visited[neighbor_rid]){
						visited[neighbor_rid] = true;
						q.push(make_pair(neighbor_rid, cur_dist + 1));
						if (neighbor_rid == dst_rid){
							shortest_paths[src_rid][dst_rid] = cur_dist + 1;
							path_found = true;
							break;
						}
					}
				}
			}
		}
	}
	return shortest_paths;
}


tuple<
map<int,map<int,long>>,				// Number of different paths from each src_rid to each dst_rid (virtual paths)
map<int,map<int,map<int,long>>>,	// Histogram of path lengths of the different paths from each src_rid to each dst_rid
map<tuple<int,int,int>,long>,		// Number of paths per virtual channel
map<pair<int,int>,long>,			// Number of paths per physical channel
map<int,int>,						// Histogram on number of choices per routing step (virtual paths)
map<int,int>						// Histogram on number of choices per routing step (physical paths)
> compute_routing_stats(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, int n_nodes, int n_vcs, routing_function& modular_routing_function, vector<int> routers_with_nodes, vector<map<int, int>> router_and_outport_to_next_router){
	// Statistics to fill in
	map<int,map<int,long>> num_virt_paths;													
	map<int,map<int,map<int,long>>> path_length_hist;
	map<pair<int,int>, long> num_paths_per_physical_channel;
	map<tuple<int,int,int>, long> num_paths_per_virtual_channel;
	map<int,int> virtual_choice_histogram;	
	map<int,int> physical_choice_histogram;	
	// Variables
	int cur_rid, nxt_rid;							// Router IDs
	int in_vc, out_vc, min_vc, max_vc;				// VCs
	int dst_nid;									// Destination node ID
	int in_port, out_port, nxt_in_port;				// Ports
	int multiplicity_virt;
	vector<tuple<int,int,int,int,int>> cur_hops, nxt_hops;  // Format: (prv_rid, cur_rid, in_port, in_vc, multiplicity_virt)
	// Iterate through all src and dst pairs.
	for (int src_rid : routers_with_nodes){
		for (int dst_rid : routers_with_nodes){
			// From one route to itself, there is one path of length 0.
			if (src_rid == dst_rid) {
				num_virt_paths[src_rid][dst_rid] = 1;
				path_length_hist[src_rid][dst_rid][0] = 1;
				continue;
			}
			in_vc = 0; 													// NOTE: For now, we assume injection is always in VC 0. It shouldn't make any difference but perhaps double-check this assumption later.
			in_port = get<1>(router_list[0][src_rid].begin()->second); 	// NOTE: For now, we just assume that the first node connected to the router is the one injecting the packet.
			dst_nid = router_list[0][dst_rid].begin()->first;
			// We keep a vector of cur_hops due to path diversity
			cur_hops.clear();
			cur_hops.push_back(make_tuple(-1, src_rid, in_port, in_vc, 1));		
			int cur_path_length = 0;
			while (!cur_hops.empty()){
				nxt_hops.clear();
				// Process current routers one by one
				for (tuple<int,int,int,int,int> cur_hop : cur_hops){
					cur_rid = get<1>(cur_hop);
					in_port = get<2>(cur_hop);
					in_vc = get<3>(cur_hop);
					multiplicity_virt = get<4>(cur_hop);  					// 
					// Get potential output ports and VCs for the current hop
					vector<tuple<int,int,int>> out_ports_and_vcs = modular_routing_function.route(router_list, n_routers, n_nodes, n_vcs, src_rid, cur_rid, in_vc, dst_nid, dst_rid, in_port);
					// Initialize routing choice counters for this hop
					int num_virtual_choice = 0;
					set<int> valid_outports;
					// Iterate through the potential output ports and VCs
					for (tuple<int,int,int> out_port_and_vcs : out_ports_and_vcs){
						out_port = get<0>(out_port_and_vcs);
						min_vc = get<1>(out_port_and_vcs);
						max_vc = get<2>(out_port_and_vcs);
						nxt_rid = router_and_outport_to_next_router[cur_rid][out_port];
						nxt_in_port = get<1>(router_list[1][cur_rid][nxt_rid]);
						// Update routing choice counters for this hop
						num_virtual_choice += (max_vc - min_vc + 1);
						valid_outports.insert(out_port);
						// Iterate through actual output VCs
						for (out_vc = min_vc; out_vc <= max_vc; out_vc++){
							// Update the number of paths per physical and virtual channel for this hop
							if (num_paths_per_physical_channel.find(make_pair(cur_rid, out_port)) == num_paths_per_physical_channel.end()){
								num_paths_per_physical_channel[make_pair(cur_rid, out_port)] = multiplicity_virt;
							} else{
								num_paths_per_physical_channel[make_pair(cur_rid, out_port)] += multiplicity_virt;
							}
							if (num_paths_per_virtual_channel.find(make_tuple(cur_rid, out_port, out_vc)) == num_paths_per_virtual_channel.end()){
								num_paths_per_virtual_channel[make_tuple(cur_rid, out_port, out_vc)] = multiplicity_virt;
							} else{
								num_paths_per_virtual_channel[make_tuple(cur_rid, out_port, out_vc)] += multiplicity_virt;
							}

							// If the next router is the destination router, we are done with this path. Otherwise, we add it to nxt_hops for further processing.
							if (nxt_rid == dst_rid){
								// Update statistics for this path
								num_virt_paths[src_rid][dst_rid] += multiplicity_virt;
								if (path_length_hist[src_rid][dst_rid].find(cur_path_length + 1) == path_length_hist[src_rid][dst_rid].end()){
									path_length_hist[src_rid][dst_rid][cur_path_length + 1] = multiplicity_virt;
								}
								else{
									path_length_hist[src_rid][dst_rid][cur_path_length + 1] += multiplicity_virt;
								}
							// Add a new hop to nxt_hops if this one does not exist yet. If it already exists, we just need to update the multiplicity of the existing hop.
							} else {
								// Sequentially search for the hop in nxt_hops to update multiplicity
								bool hop_found = false;
								for (int i = 0; i < static_cast<int>(nxt_hops.size()); i++){
									if (get<1>(nxt_hops[i]) == nxt_rid && get<2>(nxt_hops[i]) == nxt_in_port && get<3>(nxt_hops[i]) == out_vc){
										get<4>(nxt_hops[i]) += multiplicity_virt;
										hop_found = true;
										break;
									}
								}
								// If the hop was not found, we add it to nxt_hops with the current multiplicity.
								if (!hop_found){
									nxt_hops.push_back(make_tuple(cur_rid, nxt_rid, nxt_in_port, out_vc, multiplicity_virt));
								}
							}
						}
					}
					// Update statistics for this hop
					if (physical_choice_histogram.find(valid_outports.size()) == physical_choice_histogram.end()){
						physical_choice_histogram[valid_outports.size()] = 1;
					}
					else{
						physical_choice_histogram[valid_outports.size()]++;
					}
					if (virtual_choice_histogram.find(num_virtual_choice) == virtual_choice_histogram.end()){
						virtual_choice_histogram[num_virtual_choice] = 1;
					}
					else{
						virtual_choice_histogram[num_virtual_choice]++;
					}
				}
				cur_hops = nxt_hops;
				cur_path_length++;
			}
		}
	}
	return make_tuple(num_virt_paths, path_length_hist, num_paths_per_virtual_channel, num_paths_per_physical_channel, virtual_choice_histogram, physical_choice_histogram);
}

int build_cdg(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, int n_nodes, int n_vcs, routing_function& modular_routing_function, vector<int> routers_with_nodes, vector<map<int, int>> router_and_outport_to_next_router){
	cout << "Routing Analyzer: Building the CDG to detect potential deadlocks..." << endl;
	// Variables
	int prv_rid, cur_rid, nxt_rid;					// Router IDs
	int in_vc, out_vc, min_vc, max_vc;				// VCs
	int dst_nid;									// Destination node ID
	int in_port, out_port, nxt_in_port;				// Ports
	vector<tuple<int,int,int,int>> cur_hops, nxt_hops;  // Format: (prv_rid, cur_rid, in_port, in_vc)
	// Construct the CDG
	CDG_E cdg(router_list, n_routers, n_vcs);
	for (int src_rid : routers_with_nodes){
		for (int dst_rid : routers_with_nodes){
			// We do not need to analyze routing from a router to itself
			if (src_rid == dst_rid) {
				continue;
			}
			in_vc = 0; 													// NOTE: For now, we assume injection is always in VC 0. It shouldn't make any difference but perhaps double-check this assumption later.
			in_port = get<1>(router_list[0][src_rid].begin()->second); 	// NOTE: For now, we just assume that the first node connected to the router is the one injecting the packet.
			dst_nid = router_list[0][dst_rid].begin()->first;
			// We keep a vector of cur_hops due to path diversity
			cur_hops.clear();
			cur_hops.push_back(make_tuple(-1, src_rid, in_port, in_vc));		// Use -1 for prv_rid to indicate the injection
			while (!cur_hops.empty()){
				nxt_hops.clear();
				// Process current routers one by one
				for (tuple<int, int,int,int> cur_hop : cur_hops){
					prv_rid = get<0>(cur_hop);
					cur_rid = get<1>(cur_hop);
					in_port = get<2>(cur_hop);
					in_vc = get<3>(cur_hop);
					// Get potential output ports and VCs for the current hop
					vector<tuple<int,int,int>> out_ports_and_vcs = modular_routing_function.route(router_list, n_routers, n_nodes, n_vcs, src_rid, cur_rid, in_vc, dst_nid, dst_rid, in_port);
					// Iterate through the potential output ports and VCs
					int cnt = 0;
					for (tuple<int,int,int> out_port_and_vcs : out_ports_and_vcs){
						cnt++;
						out_port = get<0>(out_port_and_vcs);
						min_vc = get<1>(out_port_and_vcs);
						max_vc = get<2>(out_port_and_vcs);
						nxt_rid = router_and_outport_to_next_router[cur_rid][out_port];
						nxt_in_port = get<1>(router_list[1][cur_rid][nxt_rid]);
						// Iterate through actual output VCs
						for (out_vc = min_vc; out_vc <= max_vc; out_vc++){
							// Add the edge to the CDG: If this creates a cycle, the CDG will throw an exception and the program will terminate, which is what we want.
							// Note that this is not done if there is no previous router (i.e., fist hop with cur_rid == src_rid)
							if (prv_rid != -1){
								try{
									cdg.insert_edge(make_tuple(prv_rid, cur_rid, in_vc), make_tuple(cur_rid, nxt_rid, out_vc));
								}
								catch (const std::runtime_error& e)
								{
									cout << "Error during CDG construction: " << e.what() << endl;
									cout << "This indicates that there is a cycle in the CDG, which means that the routing function is potentially deadlock-prone." << endl;
									cout << "Terminating the program." << endl;
									cout << "This happened while adding the path from router " << cur_rid << " to router " << nxt_rid << " with VC " << out_vc << " for the src-dst pair (" << src_rid << ", " << dst_rid << ")." << endl;
									cout << "This was the " << cnt << "-th output port and VC choice for this hop (" << out_ports_and_vcs.size() << " choices in total)." << endl;
									cout << "The input port and VC for this hop at router " << cur_rid << " were " << in_port << " and " << in_vc << ", respectively." << endl;
									cout << "The previous router on this path was " << prv_rid << "." << endl;
									cout << "The output port and VC for this hop at router " << cur_rid << " were " << out_port << " and " << out_vc << ", respectively." << endl;
									exit(EXIT_FAILURE);
								}
							}
							// If the next router is the destination router, we are done with this path. Otherwise, we add it to nxt_hops for further processing.
							if (nxt_rid != dst_rid){
								if (find(nxt_hops.begin(), nxt_hops.end(), make_tuple(cur_rid, nxt_rid, nxt_in_port, out_vc)) == nxt_hops.end()){
									nxt_hops.push_back(make_tuple(cur_rid, nxt_rid, nxt_in_port, out_vc));
								}
							}
						}
					}
				}
				cur_hops = nxt_hops;
			}
		}
	}
	// If there would be a cycle, the CDG construction would have thrown an exception and the program would have terminated by now. If we reach this line, it means that no cycles were detected.
	cout << "\t-> CDG construction completed successfully. No cycles detected." << endl;
	return cdg.get_edge_count();
}

void analyze_routing(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, int n_nodes, int n_vcs, routing_function& modular_routing_function, vector<int> routers_with_nodes, vector<map<int, int>> router_and_outport_to_next_router, Statistics& stats){
	// Compute shortest paths with respect to the topology.
	map<int,map<int,int>> shortest_paths_topology = compute_shortest_paths_wrt_topology(router_list, n_routers, routers_with_nodes);
	map<int,int> hist_shortest_paths_topology = create_histogram(shortest_paths_topology);
	for (map<int,int>::iterator it = hist_shortest_paths_topology.begin(); it != hist_shortest_paths_topology.end(); ++it){
		std::map<std::string, int> path_length_stats;
		path_length_stats["path_length"] = it->first;
		path_length_stats["count"] = it->second;
		stats.routingStats()["shortest_paths_topology"].push_back(path_length_stats);
	}
	// Compute other routing statistics.
	tuple<
	map<int,map<int,long>>,				// Number of different paths from each src_rid to each dst_rid (virtual paths)
	map<int,map<int,map<int,long>>>, 	// Histogram of path lengths of the different paths from each src_rid to each dst_rid
	map<tuple<int,int,int>,long>,		// Histogram on number of paths per virtual channel
	map<pair<int,int>,long>,			// Histogram on number of paths per physical channel
	map<int,int>,						// Histogram on number of choices per routing step (virtual paths)
	map<int,int>						// Histogram on number of choices per routing step (physical paths)
	> routing_stats = compute_routing_stats(router_list, n_routers, n_nodes, n_vcs, modular_routing_function, routers_with_nodes, router_and_outport_to_next_router);
	map<int,map<int,long>> num_paths_virt = get<0>(routing_stats);
	map<int,map<int,map<int,long>>> path_length_hist = get<1>(routing_stats);
	map<tuple<int,int,int>,long> num_paths_per_virtual_channel = get<2>(routing_stats);
	map<pair<int,int>,long> num_paths_per_physical_channel = get<3>(routing_stats);
	map<int,int> virtual_paths_histogram = get<4>(routing_stats);
	map<int,int> physical_paths_histogram = get<5>(routing_stats);
	// Store number of paths (virtual)
	// For now, do not store this to make result files smaller
	/*
	for (map<int,map<int,long>>::iterator it = num_paths_virt.begin(); it != num_paths_virt.end(); ++it){
		for (map<int,long>::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2){
			std::map<std::string, int> num_paths_stats_virt;
			num_paths_stats_virt["src_rid"] = it->first;
			num_paths_stats_virt["dst_rid"] = it2->first;
			num_paths_stats_virt["path_count"] = (it2->second > INT_MAX) ? INT_MAX : (int)it2->second;  // Cap the number of paths at INT_MAX to avoid overflow issues in stats.
			stats.routingStats()["num_paths_per_src_dst_virt"].push_back(num_paths_stats_virt);
		}
	}
	*/
	// Transform the path length histograms per src-dst pair into a list of the shortest path length per such pair
	map<int,map<int,int>> shortest_paths_routing;
	for (int src_rid : routers_with_nodes){
		for (int dst_rid : routers_with_nodes){
			int min_length = INT_MAX;
			for (map<int,long>::iterator it = path_length_hist[src_rid][dst_rid].begin(); it != path_length_hist[src_rid][dst_rid].end(); ++it){
				min_length = min(min_length, it->first);
			}
			if (min_length == INT_MAX){
				cout << "Warning: No path found by the routing function from router " << src_rid << " to router " << dst_rid << "." << endl;
			}
			shortest_paths_routing[src_rid][dst_rid] = min_length;
		}
	}
	// Transform to histogram and store in stats
	map<int,int> hist_shortest_paths_routing = create_histogram(shortest_paths_routing);
	for (map<int,int>::iterator it = hist_shortest_paths_routing.begin(); it != hist_shortest_paths_routing.end(); ++it){
		std::map<std::string, int> path_length_stats;
		path_length_stats["path_length"] = it->first;
		path_length_stats["count"] = it->second;
		stats.routingStats()["shortest_paths_routing"].push_back(path_length_stats);
	}
	// Store number of paths per virtual channel in stats
	for (map<tuple<int,int,int>,long>::iterator it = num_paths_per_virtual_channel.begin(); it != num_paths_per_virtual_channel.end(); ++it){
		std::map<std::string, int> num_paths_stats_phys_chan;
		num_paths_stats_phys_chan["from_rid"] = get<0>(it->first);
		num_paths_stats_phys_chan["to_rid"] = get<1>(it->first);
		num_paths_stats_phys_chan["vc"] = get<2>(it->first);
		num_paths_stats_phys_chan["packet_count"] = (it->second > INT_MAX) ? INT_MAX : (int)it->second;  // Cap the number of paths at INT_MAX to avoid overflow issues in stats.
		stats.routingStats()["num_paths_per_virtual_channel"].push_back(num_paths_stats_phys_chan);
	}
	// Store number of paths per physical channel in stats
	for (map<pair<int,int>,long>::iterator it = num_paths_per_physical_channel.begin(); it != num_paths_per_physical_channel.end(); ++it){
		std::map<std::string, int> num_paths_stats_virt_chan;
		num_paths_stats_virt_chan["from_rid"] = it->first.first;
		num_paths_stats_virt_chan["to_rid"] = it->first.second;
		num_paths_stats_virt_chan["packet_count"] = (it->second > INT_MAX) ? INT_MAX : (int)it->second;  // Cap the number of paths at INT_MAX to avoid overflow issues in stats.
		stats.routingStats()["num_paths_per_physical_channel"].push_back(num_paths_stats_virt_chan);
	}
	// Store physical routing choice histogram in stats
	for (map<int,int>::iterator it = physical_paths_histogram.begin(); it != physical_paths_histogram.end(); ++it){
		std::map<std::string, int> physical_paths_stats;
		physical_paths_stats["n_physical_routing_choices"] = it->first;
		physical_paths_stats["count"] = it->second;
		stats.routingStats()["routing_choices_physical"].push_back(physical_paths_stats);
	}
	// Store virtual routing choice histogram in stats
	for (map<int,int>::iterator it = virtual_paths_histogram.begin(); it != virtual_paths_histogram.end(); ++it){
		std::map<std::string, int> virtual_paths_stats;
		virtual_paths_stats["n_virtual_routing_choices"] = it->first;
		virtual_paths_stats["count"] = it->second;
		stats.routingStats()["routing_choices_virtual"].push_back(virtual_paths_stats);
	}
	// Post-processing the path length histogram to print it in a more readable way
	int total_cnt = 0, has_topo_shortest = 0, has_topo_non_shortest = 0, has_routing_shortest = 0, has_routing_non_shortest = 0;
	int min_length, max_length;
	for (int src_rid : routers_with_nodes){
		for (int dst_rid : routers_with_nodes){
			max_length = 0;
			min_length = INT_MAX;
			for (map<int,long>::iterator it = path_length_hist[src_rid][dst_rid].begin(); it != path_length_hist[src_rid][dst_rid].end(); ++it){
				int path_length = it->first;
				min_length = min(min_length, path_length);
				max_length = max(max_length, path_length);
			}
			// Sanity check: If the min path length is INT_MAX, it means that no path was found by the routing function which must be a bug.
			if (min_length == INT_MAX){
				cout << "Error: No path found by the routing function from router " << src_rid << " to router " << dst_rid << "." << endl;
				exit(EXIT_FAILURE);
			}
			// Update the counters for the percentages we want to compute below.
			total_cnt += 1;
			has_topo_shortest += (shortest_paths_topology[src_rid][dst_rid] == min_length) ? 1 : 0;
			has_topo_non_shortest += (shortest_paths_topology[src_rid][dst_rid] < max_length) ? 1 : 0;
			has_routing_shortest += (shortest_paths_routing[src_rid][dst_rid] == min_length) ? 1 : 0;
			has_routing_non_shortest += (shortest_paths_routing[src_rid][dst_rid] < max_length) ? 1 : 0;
		}
	}
	// Building the CDG is the last step, because we abort if the CDG contains a cycle.
	// Do not do this for final experiments to save time. At this point, all routing functions should be tested and verified to be deadlock-free.
	// int cdg_edges = build_cdg(router_list, n_routers, n_nodes, n_vcs, modular_routing_function, routers_with_nodes, router_and_outport_to_next_router);
	// Store these percentages in stats as well as the number of edges in the CDG.
	std::map<std::string, int> general_stats;
	general_stats["percentage_topo_shortest"] = (int)std::round(100.0 * has_topo_shortest / total_cnt);
	general_stats["percentage_topo_non_shortest"] = (int)std::round(100.0 * has_topo_non_shortest / total_cnt);
	general_stats["percentage_routing_shortest"] = (int)std::round(100.0 * has_routing_shortest / total_cnt);
	general_stats["percentage_routing_non_shortest"] = (int)std::round(100.0 * has_routing_non_shortest / total_cnt);
	stats.routingStats()["general_stats"].push_back(general_stats);
}
