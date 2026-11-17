#pragma once
#include "routing_function.hpp"
#include "selection_function.hpp"
#include <vector>
#include <map>
#include <tuple>
#include <utility>  
#include <vector>
#include <string>

class CDG_E {
public:
    CDG_E(const std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>>& router_list, int n_routers, int n_vcs);
	bool has_edge(std::tuple<int,int,int> from, std::tuple<int,int,int> to) const; 										// Checks if there is an edge from 'from' to 'to'. Vertex format: (from_rid, to_rid, vc)
	bool creates_cycle(std::tuple<int,int,int> from, std::tuple<int,int,int> to) const; 								// Checks if adding an edge from 'from' to 'to' would create a cycle. Vertex format: (from_rid, to_rid, vc)
	void insert_edge(std::tuple<int,int,int> from, std::tuple<int,int,int> to); 										// Inserts an edge from 'from' to 'to'. Vertex format: (from_rid, to_rid, vc)
	void insert_edges(std::vector<std::pair<std::tuple<int,int,int>, std::tuple<int,int,int>>> edges); 					// Inserts mulitple edges at once. Each edge is represented as a pair of vertices (from, to), where each vertex is a tuple (from_rid, to_rid, vc)
	void remove_edge(std::tuple<int,int,int> from, std::tuple<int,int,int> to); 										// Removes the edge from 'from' to 'to'. Vertex format: (from_rid, to_rid, vc)
	void remove_edges(std::vector<std::pair<std::tuple<int,int,int>, std::tuple<int,int,int>>> edges); 					// Removes multiple edges at once. Each edge is represented as a pair of vertices (from, to), where each vertex is a tuple (from_rid, to_rid, vc)
	bool report_cycle(std::tuple<int,int,int> from, std::tuple<int,int,int> to) const; 									// If adding an edge from 'from' to 'to' creates a cycle, this function returns the list of vertices that form the cycle. Vertex format: (from_rid, to_rid, vc)
    bool is_cyclic();																									// Checks if the CDG contains any cycles
	int get_edge_count() const; 																						// Returns the total number of edges in the CDG

private:
	const std::vector<std::map<int,std::map<int,std::tuple<int,int,int>>>> _router_list;
	int _n_routers; 																									// Number of routers in the CDG
	int _n_vcs; 																										// Number of virtual channels in the CDG
	int _n_vertices; 																									// Total number of vertices in the CDG
    
	std::map<std::tuple<int,int,int>, int> _vertex_to_id; 																// Maps each vertex (src_rid, dst_rid, vc) to a unique integer ID for easier graph traversal
	std::vector<std::tuple<int,int,int>> _id_to_vertex; 																// Maps each vertex ID back to its corresponding vertex (src_rid, dst_rid, vc) for reference	
	std::vector<std::vector<int>> _adj_list; 																			// Adjacency list representation of the CDG, where each vertex ID maps to a list of adjacent vertex IDs
	std::vector<int> _topo_order; 																						// Stores the current topological order of the vertices in the CDG. Each element is a vertex ID corresponding to the vertex_to_id mapping.

	bool validate_topo_order(); 																						// Validates the current topological order of the CDG. Returns true if the order is valid, false otherwise.
	void recompute_topo_order(); 																						// Recomputes the topological.
};
