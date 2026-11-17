#pragma once
#include <vector>
#include <tuple>
#include <map>

class routing_function {
public:
    virtual ~routing_function() = default;
    virtual std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int cur_vc, int dst_nid, int dst_rid, int in_port) const = 0;
	virtual int get_additional_info() const {return 0;}	// Each routing function can return an additional information as an integer. Semantics of this additional information is defined by the routing function itself.
	virtual std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V) = 0; // (dense & list, dense & bitmap, csv & list, csv & bitmap)
};
