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

class a_lash_routing : public routing_function {
public:
	// Constructor
	explicit a_lash_routing(const Configuration config, std::vector<int>& routers_with_nodes, std::vector<std::map<int,int>>& router_and_outport_to_next_router);
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
	mutable std::vector<std::map<int,int>> _router_and_outport_to_next_router;					// router_and_port_to_next_router[cur_rid][(out_port)] = next_rid
	mutable std::vector<CDG_E> _cdgs; 															// once CDG for each layer

	// Routing table: routing_table[cur_rid][dst_rid] = [out_port, ...]
    mutable std::map<int,std::map<int, vector<int>>> _routing_table; 	

	// Defining the layer at which a given output-port that is on an minimal path towards the destination should be used. -1 indicates the port is not used.
	// layer_map[cur_rid][dst_rid][out_port] = [layer1, ...]
	mutable std::map<int,std::map<int,std::map<int,vector<int>>>> _layer_map; 							

	// Internal functions
	void initialize(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs);
	int find_and_commit_paths(vector<map<int,map<int,tuple<int,int,int>>>> router_list, int n_routers, int n_nodes, int n_vcs);
	void try_to_add_paths_to_muliple_layers(int n_routers, int n_vcs);
	bool are_valid_paths(vector<vector<int>> next, int src_rid, int nei_rid, int dst_rid, int layer, bool commit_paths);
};
