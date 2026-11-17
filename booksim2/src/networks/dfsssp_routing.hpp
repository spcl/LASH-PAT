#pragma once
#include "routing_function.hpp"
#include "../config_utils.hpp"
#include <vector>
#include <tuple>
#include <map>
#include <set>
#include <algorithm>
#include <string>

class dfsssp_routing : public routing_function {
public:
	// Constructor
    explicit dfsssp_routing(const Configuration config, vector<int> routers_with_nodes, vector<std::map<int,int>> router_and_outport_to_next_router);
	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][router_id][other_router_id] = (port, latency)
	// return: (out_port, vc_start, vc_end)
    std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int cur_vc, int dst_nid, int dst_rid, int in_port) const override;
	std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V); // (dense & list, dense & bitmap, csv & list, csv & bitmap)

private:
	// Auxiliary data structures
    mutable int _repetition;
    mutable string _cache_directory;
    mutable bool _is_initialized;
	mutable std::vector<int> _routers_with_nodes; 										// list of routers that are connected to nodes
	mutable std::vector<std::map<int,int>> _router_and_outport_to_next_router;          // router_and_port_to_next_router[cur_rid][(out_port)] = next_rid
	mutable int _n_layers;

	// routing_table[cur_rid][dst_rid] = out_port
    mutable std::map<int,std::map<int,int>> _routing_table; 				
	// Layer map layer_map[src_rid][dst_rid] = layer
    mutable std::map<int,std::map<int,int>> _layer_map;
    // VC map vc_map[layer] = [vc1, vc2, ...]
    mutable std::map<int,std::vector<int>> _vc_map;

	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_vcs) const;
    void find_and_commit_paths(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers) const;
	void search_and_remove_deadlocks(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_vcs) const;
};
