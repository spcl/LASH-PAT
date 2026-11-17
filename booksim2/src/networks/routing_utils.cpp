#include "routing_utils.hpp"
#include <iostream>
#include <climits>
#include <set>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <tuple>
#include <vector>

using namespace std;

void find_turn_based_paths_from_source(vector<map<int, map<int, tuple<int,int,int>>>>& router_list, int n_routers, int src_rid, map<int,map<int,map<int,vector<int>>>>& routing_table, const IsLegalTurnFn& is_legal_turn) {
	// We perform a version of Dijkstra's algorithm where vertices are (prv_rid,cur_rid) pairs with -1 representing injection.
	map<pair<int,int>, int> dist;
	map<pair<int,int>, vector<pair<int,int>>> prevs;
	set<pair<int,int>> todo;
	// Auxiliary variables (allocate only once)
	int prv_rid, cur_rid, nxt_rid, nei_rid;		// Router IDs
	int in_port, out_port;						// Ports
	int cur_distance, new_distance;				// Distance to a router from the source router
	int n_src_reached, n_src_not_reached;		// Counters to check if all paths have reached the source router during back-tracking
	pair<int,int> cur_vertex, nei_vertex;
	// Initialize the source router
	dist[make_pair(-1, src_rid)] = 0;
	todo.insert(make_pair(-1, src_rid));
	// Dijkstra's algorithm main loop
	while (!todo.empty()) {
		// Find the router with the smallest distance (and lowest ID in case of ties)
		cur_vertex = *todo.begin();
		for (pair<int,int> vertex : todo) {
			if (dist.find(vertex) == dist.end()) {
				cout << "Error in find_turn_based_paths_from_source: Vertex (" << vertex.first << "," << vertex.second << ") is in the todo set but has no distance entry." << endl;
				exit(1);
			}
			if (dist[vertex] < dist[cur_vertex] || (dist[vertex] == dist[cur_vertex] && vertex.second < cur_vertex.second)) {
				cur_vertex = vertex;
			}
		}
		todo.erase(cur_vertex);
		prv_rid = cur_vertex.first;
		cur_rid = cur_vertex.second;
		// Explore neighbors of the current vertex
		for (map<int,tuple<int,int,int>>::iterator it = router_list[1][cur_rid].begin(); it != router_list[1][cur_rid].end(); ++it) {
			nei_rid = it->first;	
			// Only process this neighbor if it is reached via a valid turn
			if (cur_vertex.first == -1 || is_legal_turn(cur_vertex.first, cur_vertex.second, nei_rid)) {
				nei_vertex = make_pair(cur_rid, nei_rid);
				new_distance = dist[cur_vertex] + 1;
				if (dist.find(nei_vertex) == dist.end() || new_distance < dist[nei_vertex]) {
					dist[nei_vertex] = new_distance;
					prevs[nei_vertex].clear();
					prevs[nei_vertex].push_back(cur_vertex);
					todo.insert(nei_vertex);
				} else if (new_distance == dist[nei_vertex]) {
					// Ensure that the same previous router is not added multiple times
					if (find(prevs[nei_vertex].begin(), prevs[nei_vertex].end(), cur_vertex) == prevs[nei_vertex].end()) {
						prevs[nei_vertex].push_back(cur_vertex);
					}
				}
			}
		}
	}
	// Use back-tracking to set the routing table entries for all nodes in the network
	for (int dst_rid = 0; dst_rid < n_routers; ++dst_rid) {
		// We skip the source router since we do not need to set routing table entries for packets that are injected at the destination router;
		if (dst_rid == src_rid) {
			continue;
		}
		// Back-track to find the next hop and set the output port accordingly
		// Instead of a single intermediate hop, we need to keep tack of multiple possible intermediate hops (due to multiple equal-cost paths)
		// In addition, instead of a single router-id per hop, we need to keep track of (cur,next) pairs to be able to check for permitted turns
		vector<pair<int,int>> intermediate_hops;
		int min_distance = INT_MAX;
		// Identify all hops that reach the destination router and only keep those that have minimal distance from the source router
		for (map<int,tuple<int,int,int>>::iterator it = router_list[1][dst_rid].begin(); it != router_list[1][dst_rid].end(); ++it) {
			cur_rid = it->first;
			if (prevs.find(make_pair(cur_rid, dst_rid)) != prevs.end()) {
				cur_distance = dist.find(make_pair(cur_rid, dst_rid)) != dist.end() ? dist[make_pair(cur_rid, dst_rid)] : INT_MAX;
				if (cur_distance < min_distance) {
					min_distance = cur_distance;
					intermediate_hops.clear();
					intermediate_hops.push_back({cur_rid, dst_rid});
				} else if (cur_distance == min_distance) {
					intermediate_hops.push_back({cur_rid, dst_rid});
				}
			}
		}
		// We use a while-true loop and break it when the source is reached (i.e. for all hop in intermediate_hops, hop.first is the source router)
		while (true) {
			// Check if we have reached the source router and need to stop back-tracking
			n_src_reached = 0;
			n_src_not_reached = 0;	
			for (pair<int,int> hop : intermediate_hops) {
				// This is the last intermediate hop; it has only one previous router (the source router)
				if (hop.first == src_rid) {
					n_src_reached++;	
				// This is not the last intermediate hop; the source router is not among the previous routers
				} else {
					n_src_not_reached++;
				}
			}
			// All paths reached the source router -> stop back-tracking
			if (n_src_reached > 0 && n_src_not_reached == 0) {
				// We need to configure the routing table entries at the source router with in_port = -1 for packets injected at the source router
				for (pair<int,int> hop : intermediate_hops) {
					cur_rid = hop.first;
					nxt_rid = hop.second;
					// Sanity check: The cur_rid should be the source router for all hops in intermediate_hops
					if (cur_rid != src_rid) {
						cout << "Error in find_turn_based_paths_from_source: Current router " << cur_rid << " is not the source router " << src_rid << " when back-tracking from router " << dst_rid << " to source router " << src_rid << ". This indicates an inconsistency in the path." << endl;
						exit(1);
					}
					// Identify the output port at the source router to reach the next router
					out_port = get<0>(router_list[1][cur_rid][nxt_rid]);
					// Set entry in routing table if not already set; Avoid duplicates
					if (find(routing_table[cur_rid][dst_rid][-1].begin(), routing_table[cur_rid][dst_rid][-1].end(), out_port) == routing_table[cur_rid][dst_rid][-1].end()) {
						routing_table[cur_rid][dst_rid][-1].push_back(out_port);
					}
				}
				break;
			// All paths have not yet reached the source router -> continue back-tracking
			} else if (n_src_reached == 0 && n_src_not_reached > 0) {
				// For all intermediate hops (i.e. paths that are explored in parallel), transition one hop towards the source router; Only keep ones that use permitted turns
				vector<pair<int,int>> new_intermediate_hops;
				for (pair<int,int> hop : intermediate_hops) {
					cur_rid = hop.first;
					nxt_rid = hop.second;
					// Sanity check: Does the current hop have a previous hop?
					if (prevs.find(hop) == prevs.end()) {
						cout << "Error in find_turn_based_paths_from_source: No previous hop found for hop (" << hop.first << "," << hop.second << ") when back-tracking from router " << dst_rid << " to source router " << src_rid << ". This indicates an inconsistency in the path." << endl;
						exit(1);
					}
					// Explore all previous hops for the current hop
					for (pair<int,int> prev_hop : prevs[hop]) {
						// Sanity-check: Do the hops match
						if (cur_rid != prev_hop.second) {
							cout << "Error in find_turn_based_paths_from_source: Current hop (" << hop.first << "," << hop.second << ") does not match previous hop (" << prev_hop.first << "," << prev_hop.second << ") when back-tracking from router " << dst_rid << " to source router " << src_rid << ". This indicates an inconsistency in the path." << endl;
							exit(1);
						}
						// Sanity-check: Is the turn from prev_hop.first to hop.second via hop.first legal?
						if (!is_legal_turn(prev_hop.first, cur_rid, nxt_rid)) {
							cout << "Error in find_turn_based_paths_from_source: Turn from router " << prev_hop.first << " to router " << nxt_rid << " via router " << cur_rid << " is not legal when back-tracking from router " << dst_rid << " to source router " << src_rid << ". This indicates an inconsistency in the path." << endl;
							exit(1);
						}
						// Identify the output port at cur_rid to reach next_rid
						prv_rid = prev_hop.first;
						in_port = get<1>(router_list[1][prv_rid][cur_rid]);
						out_port = get<0>(router_list[1][cur_rid][nxt_rid]);
						// Avoid duplicates
						if (find(routing_table[cur_rid][dst_rid][in_port].begin(), routing_table[cur_rid][dst_rid][in_port].end(), out_port) == routing_table[cur_rid][dst_rid][in_port].end()) {
							routing_table[cur_rid][dst_rid][in_port].push_back(out_port);
						}
						// Continue back-tracking by adding (prev, cur) to the new intermediate hops.
						// Do not add any duplicates to the list of new intermediate hops; This means that dst->src paths can converge (i.e., src->dst paths can diverge)
						if (find(new_intermediate_hops.begin(), new_intermediate_hops.end(), make_pair(prv_rid, cur_rid)) == new_intermediate_hops.end()) {
							new_intermediate_hops.push_back({prv_rid, cur_rid});
						}
					}
				}
				// Update the intermediate hops for the next iteration
				intermediate_hops = new_intermediate_hops;
			// Error: Some paths reached the source router while others have not -> this should not happen in equal-cost path routing
			} else if (n_src_reached > 0 && n_src_not_reached > 0) {
				cout << "Error in find_turn_based_paths_from_source: Some paths reached the source router " << src_rid << " while others have not when back-tracking from router " << dst_rid << " to source router " << src_rid << ". This indicates an inconsistency in the path." << endl;
				exit(1);
			// Error: No paths exist (this should not happen)
			} else {
				cout << "Error in find_turn_based_paths_from_source: No paths found from destination router " << dst_rid << " to source router " << src_rid << " during back-tracking. This indicates an inconsistency in the path." << endl;
				exit(1);
			}
		}
	}	
}


tuple<int,int,int,int> get_max_size_of_turn_based_routing_table(map<int, map<int, map<int, vector<int>>>> routing_table, int N, int P, int V){
	// Variables to be set based on routing table: C: Max number of routing choices per hop (over all cur_rids, dst_rids, (in-port, in-vc)-pairs), E: Max number of entries in the routing table (over all cur_rids)
	int C = 0, E = 0;
	// Number of destinations equals number of routers with nodes
	for (map<int, map<int, map<int, vector<int>>>>::const_iterator it = routing_table.begin(); it != routing_table.end(); ++it) {
		int E_tmp = 0;
		for (map<int, map<int, vector<int>>>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
			for (map<int, vector<int>>::const_iterator it3 = it2->second.begin(); it3 != it2->second.end(); ++it3) {
				E_tmp++;
				C = max(C, static_cast<int>(it3->second.size()));
			}
		}
		E = max(E, E_tmp);
	}
	// Compute table size based on variables identified above
	int dense_rows = N * (P + 1);
	int bits_list = C * ceil(log2(P));
	int bits_bitmap = P;
	int csr_it_rows = N * (P + 1);
	int csr_it_bits = ceil(log2(E));
	int csr_rows = E;
	int dense_list = dense_rows * bits_list;
	int dense_bitmap = dense_rows * bits_bitmap;
	int csr_list = csr_it_rows * csr_it_bits + csr_rows * bits_list;
	int csr_bitmap = csr_it_rows * csr_it_bits + csr_rows * bits_bitmap;
	return make_tuple(dense_list, dense_bitmap, csr_list, csr_bitmap);
}
