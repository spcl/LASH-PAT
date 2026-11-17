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

class lash_pat_routing : public routing_function {
public:
	// Constructor
	explicit lash_pat_routing(const Configuration config, std::unique_ptr<routing_function> fallback, std::vector<int> routers_with_nodes, std::vector<std::map<int,int>> router_and_outport_to_next_router);
	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][router_id][other_router_id] = (port, latency)
	// return: (out_port, vc_start, vc_end)
    std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const override;
	// Return additional info: 0 = undefined, 1 = custom base routing, 2 = fallback base routing;
	int get_additional_info() const override;
	std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V); // (dense & list, dense & bitmap, csv & list, csv & bitmap)

private:
	// Definition of the GetValidHops function
	using GetValidHopsFn = std::vector<std::pair<int,int>> (lash_pat_routing::*)(
    std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>, // router_list
	CDG_E*,
    int, int, int,   // n_routers, n_nodes, n_layers
    int, std::vector<std::tuple<int,int,int>>, int, int,   // src_rid, prv_hpos, cur_rid, in_layer
    int, int, int   // dst_nid, dst_rid, in_port
	) const;

	// Auxiliary data structures
	mutable int _additional_info; 																// additional info to return in get_additional_info()
	mutable int _repetition;
    mutable string _cache_directory;
	mutable int _n_additional_paths;
	mutable int _n_layers;
	mutable string _cost_aggregation;
	mutable double _existing_path_bias;
	mutable string _cost_function_base_type;
	mutable string _cost_function_multipath_type;
	mutable bool _is_initialized;
	mutable bool _is_in_multipath_phase; 														// whether we are in the multipath phase or not
	mutable std::vector<int> _routers_with_nodes_1; 											// list of routers that are connected to nodes, randomly shuffled
	mutable std::vector<int> _routers_with_nodes_2; 											// list of routers that are connected to nodes, randomly shuffled in a different order
	mutable std::vector<std::map<int,int>> _router_and_outport_to_next_router;					// router_and_port_to_next_router[cur_rid][(out_port)] = next_rid
	mutable std::vector<std::map<int,int>> _router_and_inport_to_prev_router;					// router_and_port_to_next_router[cur_rid][(in_port)] = prev_rid
	std::unique_ptr<routing_function> _fallback_routing_function; 								// fallback routing function
    double (lash_pat_routing::*_cost_function_base)(int, int, int, int) const;
    double (lash_pat_routing::*_cost_function_multipath)(int, int, int, int) const;

	// Routing table: routing_table[cur_rid][dst_rid][(in_port,in_layer)]= [(out_port, out_layer, path_length, next_rid), ...]
	// Note: path_length and next_rid are only needed while computing the routing table, they would not be stored in the final routing table in hardware.
    mutable std::map<int,std::map<int,std::map<std::pair<int,int>, std::vector<std::tuple<int,int,int,int>>>>> _routing_table; 	

	// Statistics
	// paths_per_physical_channel[from_rid][to_rid] = #paths
	mutable std::map<std::pair<int,int>, double> _paths_per_physical_channel;
	mutable std::map<std::tuple<int,int,int>, double> _paths_per_virtual_channel;
	mutable std::map<std::tuple<int,int,int,int>, double> _paths_per_virtual_channel_per_dst;

	// Internal functions
    double cost_function_length(int from_rid, int to_rid, int layer, int n_routers) const;
    double cost_function_length_and_layer(int from_rid, int to_rid, int layer, int n_routers) const;
    double cost_function_length_and_pc_utilization(int from_rid, int to_rid, int layer, int n_routers) const;
    double cost_function_length_and_vc_utilization(int from_rid, int to_rid, int layer, int n_routers) const;
    double cost_function_length_and_combined_utilization(int from_rid, int to_rid, int layer, int n_routers) const;
	std::vector<std::pair<int,int>> get_valid_hops_using_fallback_routing(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, CDG_E* cdg, int n_routers, int n_nodes, int n_layers, int src_rid, std::vector<std::tuple<int,int,int>> prv_hops, int cur_rid, int in_layer, int dst_nid, int dst_rid, int in_port) const;
	std::vector<std::pair<int,int>> get_valid_hops_using_cdg(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, CDG_E* cdg, int n_routers, int n_nodes, int n_layers, int src_rid, std::vector<std::tuple<int,int,int>> prv_hops, int cur_rid, int in_layer, int dst_nid, int dst_rid, int in_port) const;
	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers);
	bool find_and_commit_paths(CDG_E* cdg, std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops, int prv_rid, int src_rid, int dst_rid, int in_port, int in_layer, double flow, double max_path_cost);
	std::pair<bool,CDG_E*> find_and_commit_paths_base_routing(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops);
	bool find_and_commit_paths_multipath_routing(CDG_E* cdg, std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops);
	bool improve_paths(CDG_E* cdg, vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_layers, GetValidHopsFn get_valid_hops);
	void commit_hop(CDG_E* cdg, int dst_rid, int prv_rid, int cur_rid, int cur_in_port, int cur_in_layer, int cur_out_port, int cur_out_layer, int nxt_rid, int path_length, double flow);
	void remove_hop(CDG_E* cdg, int dst_rid, int prv_rid, int cur_rid, int cur_in_port, int cur_in_layer, int cur_out_port, int cur_out_layer, int nxt_rid); 
	void propagate_channel_utilization_change(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int in_port, int in_layer, int cur_rid, int dst_rid, double flow_change);
	void clear_routing_table_and_stats();
};
