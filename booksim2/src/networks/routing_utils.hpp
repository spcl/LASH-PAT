#pragma once

#include <vector>
#include <map>
#include <tuple>
#include <functional>

// alias for readability
using IsLegalTurnFn = std::function<bool(int,int,int)>;

void find_turn_based_paths_from_source(std::vector<std::map<int, std::map<int, std::tuple<int,int,int>>>>& router_list, int n_routers, int src_rid, std::map<int,std::map<int,std::map<int,std::vector<int>>>>& routing_table, const IsLegalTurnFn& is_legal_turn);

std::tuple<int,int,int,int> get_max_size_of_turn_based_routing_table(std::map<int, std::map<int, std::map<int, std::vector<int>>>> routing_table, int N, int P, int V);
