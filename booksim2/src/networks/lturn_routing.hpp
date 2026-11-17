#pragma once
#include "routing_function.hpp"
#include "../config_utils.hpp"
#include <vector>
#include <tuple>
#include <map>
#include <set>

class lturn_routing : public routing_function {
public:
	// Constructor
	explicit lturn_routing(const Configuration config);
	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][router_id][other_router_id] = (output_port_at_src_router, input_port_at_dst_router, latency)
	// return: (out_port, vc_start, vc_end)
    std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int cur_vc, int dst_nid, int dst_rid, int in_port) const override;
	std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V); // (dense & list, dense & bitmap, csv & list, csv & bitmap)

private:
	// Channel types
	enum class ChannelType { LU, LD, RU, RD };

	// Internal state
	mutable bool _is_initialized;
	mutable int _repetition;
    mutable string _cache_directory;

	// Store router levels and widths and prohibited turns
	mutable std::map<int,int> _router_levels;
	mutable std::map<int,int> _router_widths;
	mutable std::set<std::tuple<int,int,int>> _explicitly_prohibited_turns; // (prv_rid, cur_rid, nxt_rid)

	// routing_table[cur_router_id][dst_router_id][in_port] = (out_port_1, ..., out_port_n)
    mutable std::map<int,std::map<int,std::map<int,std::vector<int>>>> _routing_table;

	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][src_router_id][dst_router_id] = (output_port_at_src, input_port_at_dst, latency)
	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers);
	int compute_router_levels_and_widths(std::vector<std::map<int, std::map<int, std::tuple<int, int, int>>>> router_list, int n_routers) const;
	void compute_explicitly_prohibited_turns(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int root_rid) const;
	ChannelType get_channel_type(int from_rid, int to_rid) const;
	bool is_legal_turn(int prv_rid, int cur_rid, int nxt_rid) const;
};
