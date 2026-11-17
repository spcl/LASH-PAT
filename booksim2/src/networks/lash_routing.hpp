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

class lash_routing : public routing_function {
public:
	// Constructor
	explicit lash_routing(const Configuration config, vector<int> routers_with_nodes);
	// router_list[0][router_id][node_id] = (port, latency)
	// router_list[1][router_id][other_router_id] = (port, latency)
	// return: (out_port, vc_start, vc_end)
    std::vector<std::tuple<int,int,int>> route(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs, int src_rid, int cur_rid, int in_vc, int dst_nid, int dst_rid, int in_port) const override;
	std::tuple<int,int,int,int> get_max_routing_table_size(int N, int P, int V); // (dense & list, dense & bitmap, csv & list, csv & bitmap)

private:

	// Auxiliary data structures
	mutable int _repetition;
    mutable string _cache_directory;
	mutable bool _is_initialized;
	mutable std::vector<int> _routers_with_nodes; 												// list of routers that are connected to nodes

	// CDG for each layer and number of layers
	mutable int _n_layers;
	mutable std::vector<CDG_E> _cdgs; 

	// Routing table: routing_table[cur_rid][dst_rid] = out_port
    mutable std::map<int,std::map<int, int>> _routing_table; 	
	// Layer map layer_map[src_rid][dst_rid] = layer
	mutable std::map<int,std::map<int,int>> _layer_map;
	// VC map vc_map[layer] = [vc1, vc2, ...]
	mutable std::map<int,vector<int>> _vc_map;

	// Internal functions
	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs);
	void find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_vcs, int dst_rid);
	void assign_path_to_layer(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_vcs, int src_rid, int dst_rid, vector<int> path);
};
