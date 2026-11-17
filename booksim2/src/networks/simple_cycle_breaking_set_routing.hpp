#pragma once
#include "routing_function.hpp"
#include "../config_utils.hpp"
#include <vector>
#include <tuple>
#include <map>

class simple_cycle_breaking_set_routing : public routing_function {
public:
	// Constructor
	explicit simple_cycle_breaking_set_routing(const Configuration config);
	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][router_id][other_router_id] = (output_port_at_src_router, input_port_at_dst_router, latency)
	// return: (out_port, vc_start, vc_end)
    std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int cur_vc, int dst_nid, int dst_rid, int in_port) const override;
	std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V); // (dense & list, dense & bitmap, csv & list, csv & bitmap)

private:
	mutable bool _is_initialized;
	mutable int _repetition;
    mutable string _cache_directory;

	// stores permitted and forbidden turns as (prev_router_id, cur_router_id, next_router_id)
	mutable std::vector<std::tuple<int,int,int>> _permitted_turns;
    mutable std::vector<std::tuple<int,int,int>> _forbidden_turns;

	// routing_table[cur_router_id][dst_router_id][in_port] = (out_port_1, ..., out_port_n)
    mutable std::map<int,std::map<int,std::map<int,std::vector<int>>>> _routing_table;

	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][src_router_id][dst_router_id] = (output_port_at_src, input_port_at_dst, latency)
	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers);
	void compute_permitted_turns(std::vector<std::map<int, std::map<int, std::tuple<int, int, int>>>> router_list, int n_routers) const;
};
