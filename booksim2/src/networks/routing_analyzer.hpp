#ifndef ROUTING_ANALYZER_HPP
#define ROUTING_ANALYZER_HPP

#include <memory>
#include <vector>
#include <map>
#include <tuple>
#include "statistics.hpp"

class routing_function;

void analyze_routing(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, int n_nodes, int n_vcs, routing_function& modular_routing_function, std::vector<int> routers_with_nodes, std::vector<std::map<int, int>> router_and_outport_to_next_router, Statistics& stats);
int build_cdg(std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, int n_nodes, int n_vcs, routing_function& modular_routing_function, std::vector<int> routers_with_nodes, std::vector<std::map<int, int>> router_and_outport_to_next_router);

#endif
