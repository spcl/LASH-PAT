#pragma once
#include <memory>
#include "../config_utils.hpp"
#include "edge_based_cdg.hpp"
#include "routing_function.hpp"
#include <vector>
#include <string>
#include <tuple>
#include <map>
#include <limits>

class lash_tor_routing : public routing_function {
public:
	// Constructor
	explicit lash_tor_routing(const Configuration config, std::unique_ptr<routing_function> fallback, vector<int> routers_with_nodes, vector<map<int,int>> router_and_outport_to_next_router);
	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][router_id][other_router_id] = (port, latency)
	// return: (out_port, vc_start, vc_end)
    std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const override;
	// Return additional info: 0 = undefined, 1 = no fallback layer, 2 = fallback layer
	int get_additional_info() const override;
	std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V); // (dense & list, dense & bitmap, csv & list, csv & bitmap)

private:
	// Auxiliary data structures
	mutable int _repetition;
    mutable string _cache_directory;
	mutable bool _is_initialized;
	mutable int _additional_info; 															    // 0 = undefined, 1 = no fallback layer, 2 = fallback layer
	mutable int _n_layers;

	mutable std::vector<int> _routers_with_nodes; 												// list of routers that are connected to nodes
	mutable std::vector<std::map<int,int>> _router_and_outport_to_next_router;					// router_and_port_to_next_router[cur_rid][(out_port)] = next_rid
	mutable std::vector<CDG_E> _cdgs; 															// once CDG for each layer

	std::unique_ptr<routing_function> _fallback_routing_function; 								// fallback routing function

	// Routing table: routing_table[cur_rid][dst_rid][(in_port,in_layer)]= [(out_port, out_layer, path_length, next_rid), ...]
	// Note: next_rid is only here to facilitate computing the routing tables. It would not be in a routing table in hardware and is not needed for routing.
    mutable std::map<int,std::map<int,std::map<std::pair<int,int>, std::tuple<int,int,int>>>> _routing_table; 	

	// Internal functions
	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers);
	bool find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, bool use_fallback_layer);
	bool is_valid_hop(vector<int> path, int nxt_rid, int layer);
	bool is_valid_path(vector<int> path, int layer);
};
