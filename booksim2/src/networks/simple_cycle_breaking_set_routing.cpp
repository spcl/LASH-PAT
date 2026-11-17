#include "simple_cycle_breaking_set_routing.hpp"
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

using namespace std;

// Constructor
simple_cycle_breaking_set_routing::simple_cycle_breaking_set_routing(const Configuration config) {
    _repetition = config.GetInt("repetition");
    _cache_directory = config.GetStr("path_for_cache");
	_is_initialized = false;
}

vector<tuple<int,int,int>> simple_cycle_breaking_set_routing::route(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const {
	// Initialize, i.e. compute routing tables
	if (!_is_initialized) {
		// First, check if a routing table for this topology and configuration is already stored in the cache
		size_t topology_hash = router_hash::hash_router_list(router_list);
		string rt_cache_file = _cache_directory + "simple_cycle_breaking_set_routing_table_" + to_string(topology_hash) + "_rep" + to_string(_repetition) + ".bin";
		cout << "Checking for cached routing tables at " << rt_cache_file << "..." << endl;
		if (filesystem::exists(rt_cache_file)) {
			// Load routing tables from cache
			RoutingTableType2 rt;
			rt.load(rt_cache_file);
			_routing_table = rt._routing_table;
			cout << "- Loaded SCB routing tables from cache: " << rt_cache_file << endl;
		} else {
			cout << "- No cached routing tables found for this topology and configuration. Computing routing tables..." << endl;
			// Hack to call non-constant initialize function from constant route function.
			const_cast<simple_cycle_breaking_set_routing*>(this)->initialize(router_list, n_routers);
			// Store routing tables in cache for future runs (if not already stored by a different thread)
			if (!filesystem::exists(rt_cache_file)) {
				RoutingTableType2 rt;
				rt._routing_table = _routing_table;
				rt.save(rt_cache_file);
				cout << "Saved SCB routing tables to cache: " << rt_cache_file << endl;
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

void simple_cycle_breaking_set_routing::initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers){
	// Compute the permitted and forbidden turns for the simple cycle-breaking set strategy
	compute_permitted_turns(router_list, n_routers);
	// Compute valid next hops for all source-destination pairs based on the computed permitted and forbidden turns
	for (int src_rid = 0; src_rid < n_routers; ++src_rid) {
		find_turn_based_paths_from_source(router_list, n_routers, src_rid, _routing_table, [this](int a,int b,int c){ return std::find(_permitted_turns.begin(), _permitted_turns.end(), std::make_tuple(a, b, c)) != _permitted_turns.end();});
	}
	// Mark the routing table as initialized
	_is_initialized = true;
}

void simple_cycle_breaking_set_routing::compute_permitted_turns(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers) const {
	// Preprocessing: Compute a clean adjacency list for easier handling
	std::vector<std::vector<int>> adj_list(n_routers);
	for (int src_rid = 0; src_rid < n_routers; ++src_rid) {
		for (std::map<int,std::tuple<int,int,int>>::iterator it = router_list[1][src_rid].begin(); it != router_list[1][src_rid].end(); ++it) {
			int dst_rid = it->first;
			adj_list[src_rid].push_back(dst_rid);
		}
	}
	// Boolean vector to invalidate vertices that have been processed (removed from the graph)
	std::vector<bool> is_valid(n_routers, true);
	// Instead of n-1 iterations, we perform n-2 iterations since the case |V|==2 is only used for labeling (which we don't do) and not for forbidding turns
	for (int stage = 0; stage < n_routers - 2; ++stage) {
		// Compute the degree of each vertex; only valid vertices are considered (we operate on the graph G' that is a subgraph of G induced by the valid vertices)
		// --------------------------------------------------------------------------------------------------------------------------------------------------------
		std::vector<int> vertex_degree(n_routers, 0);
		for (int rid = 0; rid < n_routers; ++rid) {
			if (is_valid[rid]) {
				for (int nei_rid : adj_list[rid]) {
					if (is_valid[nei_rid]) {
						vertex_degree[rid]++;
					}
				}
			}
		}
		// Check for each vertex whether it is a cut-node or not. 
		// This is done by using Tarjan's algorithm to find all cut-vertices in O(V+E) time (A variant of DFS is used)
		// --------------------------------------------------------------------------------------------------------------------------------------------------------
		std::vector<bool> is_cut_vertex(n_routers, false);
		// Auxiliary data structures for Tarjan's algorithm
		// disc[i]: 	discovery time of node i in DFS order
		// low[i]:  	lowest discovery time reachable from i (including back edges)
		// parent[i]: 	DFS parent of node i
		std::vector<int> disc(n_routers, -1), low(n_routers, -1), parent(n_routers, -1);
		// Global timer for discovery times in DFS
		int tim = 0;
		// Recursive lambda function for DFS
		auto dfs = [&](auto&& self, int u) -> void {
			// Initialize discovery time and low value
			disc[u] = low[u] = tim++;
			// Count of children in DFS
			int child = 0;
			// Explore all neighbors
			for (int v : adj_list[u]) {
				// Skip invalid neighbors
				if (!is_valid[v]) continue;
				// If v is not visited yet, recurse on it
				if (disc[v] == -1) {
					// Set parent of v to u (the current node)
					parent[v] = u;
					// Increment child count for u
					++child;
					// Recursive DFS call for neighbor v
					self(self, v);
					// Update low value of u based on the low value of v
					low[u] = std::min(low[u], low[v]);
					// Rule 1: if u is the root of DFS and has two or more children, then u is a cut-vertex
					if (parent[u] == -1 && child > 1) is_cut_vertex[u] = true;
					// Rule 2: if u is not the root and no back edge from v or its descendants points to one of u's ancestors, then u is a cut-vertex
					if (parent[u] != -1 && low[v] >= disc[u]) is_cut_vertex[u] = true;
				// If v is already visited and is not the parent of u, then it's a back edge -> update low value of u
				} else if (v != parent[u]) {
					low[u] = std::min(low[u], disc[v]);
				}
			}
		};
		// Run DFS on each valid vertex that has not yet been visited (for a connected induced subgraph, this is only needed once)
		for (int i = 0; i < n_routers; ++i) {
			if (is_valid[i] && disc[i] == -1) {
				parent[i] = -1;
				dfs(dfs, i);
			}
		}
		// Identify the vertex with the smallest degree that is not a cut-vertex and that satisfies the degree condition.
		// One such vertex exists according to lemma 2 in the paper.
		// --------------------------------------------------------------------------------------------------------------------------------------------------------
		int selected_vertex_id = -1;
		int selected_vertex_degree = INT_MAX;
		int lhs_of_inquality, rhs_of_inquality;
		for (int rid = 0; rid < n_routers; ++rid) {
			// Valid vertex (i.e. not removed yet) and not a cut-vertex
			if (is_valid[rid] && !is_cut_vertex[rid]){
				// LHS only depends on the degree of the vertex
				lhs_of_inquality = vertex_degree[rid] * (vertex_degree[rid] - 1);
				// RHS depends on the degrees of the neighbors
				rhs_of_inquality = 0;
				for (int nei_rid : adj_list[rid]) {
					if (is_valid[nei_rid]) {
						rhs_of_inquality += (vertex_degree[nei_rid] - 1);
					}
				}
				// Check the degree condition
				if (lhs_of_inquality <= rhs_of_inquality) {
					// This is a valid candidate; check if it has the smallest degree found so far
					if (vertex_degree[rid] < selected_vertex_degree) {
						selected_vertex_id = rid;
						selected_vertex_degree = vertex_degree[rid];
					}
				}
			}
		}
		// Identify new prohibited turns based on the selected vertex
		// --------------------------------------------------------------------------------------------------------------------------------------------------------
		for (int prev : adj_list[selected_vertex_id]) {
			if (is_valid[prev]) {
				for (int next : adj_list[selected_vertex_id]) {
					if (is_valid[next] && next != prev) {
						std::tuple<int,int,int> turn = std::make_tuple(prev, selected_vertex_id, next);
						std::tuple<int,int,int> reverse_turn = std::make_tuple(next, selected_vertex_id, prev);
						// Make sure each turn is only added once (no duplicates)
						if (std::find(_forbidden_turns.begin(), _forbidden_turns.end(), turn) == _forbidden_turns.end()){
							_forbidden_turns.push_back(turn);
						}
						// Reverse turn
						if (std::find(_forbidden_turns.begin(), _forbidden_turns.end(), reverse_turn) == _forbidden_turns.end()){
							_forbidden_turns.push_back(reverse_turn);
						}
					}
				}
			}
		}
		// Identify new permitted turns based on the selected vertex
		// --------------------------------------------------------------------------------------------------------------------------------------------------------
		for (int next : adj_list[selected_vertex_id]) {
			if (is_valid[next]) {
				for (int nextnext: adj_list[next]) {
					if (is_valid[nextnext] && nextnext != selected_vertex_id) {
						std::tuple<int,int,int> turn = std::make_tuple(selected_vertex_id, next, nextnext);
						std::tuple<int,int,int> reverse_turn = std::make_tuple(nextnext, next, selected_vertex_id);
						// Make sure each turn is only added once (no duplicates)
						if (std::find(_permitted_turns.begin(), _permitted_turns.end(), turn) == _permitted_turns.end()){
							_permitted_turns.push_back(turn);	
						}
						// Reverse turn
						if (std::find(_permitted_turns.begin(), _permitted_turns.end(), reverse_turn) == _permitted_turns.end()){
							_permitted_turns.push_back(reverse_turn);
						}
					}
				}
			}
		}
		// Remove the selected vertex from the graph (invalidate it)
		// --------------------------------------------------------------------------------------------------------------------------------------------------------
		is_valid[selected_vertex_id] = false;
	}
}

// N: Number of routers with nodes attached, P: Network radix (number of ports to other routers), V: Number of VCs
tuple<int,int,int,int> simple_cycle_breaking_set_routing::get_max_routing_table_size(int N, int P, int V) {
	return get_max_size_of_turn_based_routing_table(_routing_table, N, P, V);
}
