#include "edge_based_cdg.hpp"
#include <iostream>
#include <cassert>
#include <stack>
#include <set>
#include <map>
#include <climits>
#include <vector>
#include <utility>
#include <queue>
#include <functional>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <string>
#include <unordered_set>



using namespace std;

// Initializes CDG_E with nodes (one node per virtual channel) but does not add any edges yet.
CDG_E::CDG_E(const vector<map<int,map<int,tuple<int,int,int>>>>& router_list, int n_routers, int n_vcs) 
	: _router_list(router_list), _n_routers(n_routers), _n_vcs(n_vcs) {
	// Variables used throughput function
	int cur_rid, nxt_rid, vc;
	// Iterate through routers
	for (cur_rid = 0; cur_rid < n_routers; ++cur_rid) {
		// Iterate through outgoing links of the router
		for (const pair<const int, tuple<int,int,int>>& item : router_list.at(1).at(cur_rid)) {
			nxt_rid = item.first;
			// Iterate through virtual channels
			for (vc = 0; vc < n_vcs; ++vc) {
				// Initialize adjacency list with empty vectors for each virtual channel
				_vertex_to_id[make_tuple(cur_rid, nxt_rid, vc)] = static_cast<int>(_vertex_to_id.size());
				_id_to_vertex.push_back(make_tuple(cur_rid, nxt_rid, vc));
			}
		}
	}
	// Initialize adjacency list and topological order.
	_n_vertices = static_cast<int>(_vertex_to_id.size());
	_adj_list.resize(_n_vertices);
	_topo_order.resize(_n_vertices);
}

void CDG_E::recompute_topo_order() {
	// Compute indegree of each node in the CDG_E
    std::vector<int> indeg(_n_vertices, 0);
    for (int u = 0; u < _n_vertices; u++) {
        const std::vector<int>& neighbors = _adj_list[u];

        for (int v : neighbors) {
            indeg[v]++;
        }
    }
	// Enqueue all nodes with indegree 0
    std::queue<int> q;
    for (int i = 0; i < _n_vertices; i++) {
        if (indeg[i] == 0) {
            q.push(i);
        }
    }
	// Perform Kahn's algorithm for topological sorting
    int idx = 0;
    while (!q.empty()) {
        int cur = q.front();
        q.pop();
        _topo_order[cur] = idx++;
        const std::vector<int>& neighbors = _adj_list[cur];
        for (int v : neighbors) {
            indeg[v]--;
            if (indeg[v] == 0) {
                q.push(v);
            }
        }
    }
	// If we were not able to assign a topological order to all nodes, it means there is a cycle in the CDG_E, 
	// which should never happen since CDG_E must always be acyclic. We throw an error in this case.
    if (idx != _n_vertices) {
        throw std::runtime_error("Error in edge_based_cdg.cpp::recompute_topo_order: the CDG_E contains a cycle, which should never happen.");
	}
}

bool CDG_E::has_edge(tuple<int,int,int> from, tuple<int,int,int> to) const {
	int from_id = _vertex_to_id.at(from);
	int to_id = _vertex_to_id.at(to);
	// Check if the edge exists
	if (find(_adj_list[from_id].begin(), _adj_list[from_id].end(), to_id) != _adj_list[from_id].end()) {
		return true;
	}
	return false;
}

bool CDG_E::creates_cycle(tuple<int,int,int> from, tuple<int,int,int> to) const {
    int u = _vertex_to_id.at(from);
    int v = _vertex_to_id.at(to);

    // If 'from' has a lower topological order than 'to', then adding an edge cannot create a cycle.
    if (_topo_order[u] < _topo_order[v]) {
        return false;
    }

    // DFS from 'to' to see if we can reach 'from'
    stack<int> st;
    vector<char> visited(_n_vertices, 0);
    st.push(v);
    while (!st.empty()) {
        int cur = st.top();
        st.pop();
		// If we reach 'from', it means adding the edge would create a cycle.
        if (cur == u) {
            return true;
        }
        if (visited[cur]) {
            continue;
        }
        visited[cur] = 1;
        // Explore neighbors
        const vector<int>& neighbors = _adj_list[cur];
        for (size_t i = 0; i < neighbors.size(); ++i) {
            int nxt = neighbors[i];
			if (!visited[nxt]) {
				st.push(nxt);
			}
        }
    }
    // No path found -> no cycle
    return false;
}

bool CDG_E::report_cycle(tuple<int,int,int> from, tuple<int,int,int> to) const {
    int u = _vertex_to_id.at(from);
    int v = _vertex_to_id.at(to);

    // If 'from' has a lower topological order than 'to', then adding an edge cannot create a cycle.
    if (_topo_order[u] < _topo_order[v]) {
        return false;
    }

    // DFS from 'to' to see if we can reach 'from'
    stack<int> st;
    vector<char> visited(_n_vertices, 0);
    vector<int> parent(_n_vertices, -1);

    st.push(v);

	vector<int> cycle; 

    while (!st.empty()) {
        int cur = st.top();
        st.pop();

        // If we reach 'from', it means adding the edge would create a cycle.
        if (cur == u) {
            // Reconstruct path v -> ... -> u
            cycle.clear();

            int x = u;
            while (x != -1) {
                cycle.push_back(x);
                if (x == v) break;
                x = parent[x];
            }

            // currently: u -> ... -> v, reverse to get v -> ... -> u
            std::reverse(cycle.begin(), cycle.end());

            // add closing edge u -> v to complete cycle
            cycle.push_back(v);

			// Print the cycle
			cout << "Adding edge (" << get<0>(from) << ", " << get<1>(from) << ", " << get<2>(from) << ") -> (" << get<0>(to) << ", " << get<1>(to) << ", " << get<2>(to) << ") would create the following cycle in the CDG: ";
			for (size_t i = 0; i < cycle.size(); ++i) {
				int node_id = cycle[i];
				tuple<int,int,int> vertex = _id_to_vertex[node_id];
				cout << "{" << get<0>(vertex) << ", " << get<1>(vertex) << ", " << get<2>(vertex) << "}";
				if (i != cycle.size() - 1) {
					cout << " => ";
				}
			}
			cout << endl;

            return true;
        }

        if (visited[cur]) {
            continue;
        }
        visited[cur] = 1;

        // Explore neighbors
        const vector<int>& neighbors = _adj_list[cur];

        for (size_t i = 0; i < neighbors.size(); ++i) {
            int nxt = neighbors[i];

            if (!visited[nxt]) {
                if (parent[nxt] == -1) {
                    parent[nxt] = cur;
                }
                st.push(nxt);
            }
        }
    }

    // No path found -> no cycle
    return false;
}


void CDG_E::insert_edge(tuple<int,int,int> from, tuple<int,int,int> to) {
	// Check if adding this edge would create a cycle in the CDG_E. If it does, we throw an error since CDG_E must remain acyclic.
	if (creates_cycle(from, to)) {
		// Find and print the cycle that would be created by adding this edge for debugging purposes.
		report_cycle(from, to);
		throw runtime_error("Error in edge_based_cdg.cpp::insert_edge: adding this edge would create a cycle in the CDG_E.");
	}
	insert_edges(vector<pair<tuple<int,int,int>, tuple<int,int,int>>>{make_pair(from, to)});
}

// Note that the function to insert a single edge checks if this edge could create a cycle,
// while the one to insert multiple edges assumes that the input edges are valid and do not create any cycles.
void CDG_E::insert_edges(vector<pair<tuple<int,int,int>, tuple<int,int,int>>> edges) {
	int from_id, to_id;
	for (pair<tuple<int,int,int>, tuple<int,int,int>> edge : edges) {
		from_id = _vertex_to_id.at(edge.first);
		to_id = _vertex_to_id.at(edge.second);
		// Check if the edge already exists to avoid duplicates
		if (find(_adj_list[from_id].begin(), _adj_list[from_id].end(), to_id) != _adj_list[from_id].end()) {
			continue;
		}
		// Add the new edge
		_adj_list[from_id].push_back(to_id);
	}
	// Check if the old topological order is still valid after adding the new edges. If it is not, we recompute a valid topological order.
	if (!validate_topo_order()) {
		recompute_topo_order();
	}
}

void CDG_E::remove_edge(tuple<int,int,int> from, tuple<int,int,int> to) {
	// Check if the edge exists before trying to remove it. If it doesn't exist, we can simply return without doing anything.
	if (!has_edge(from, to)) {
		return;
	}
	// Remove edge from the adjacency list
	int from_id = _vertex_to_id.at(from);
	int to_id = _vertex_to_id.at(to);
	_adj_list[from_id].erase(remove(_adj_list[from_id].begin(), _adj_list[from_id].end(), to_id), _adj_list[from_id].end());
}

void CDG_E::remove_edges(vector<pair<tuple<int,int,int>, tuple<int,int,int>>> edges) {
	tuple<int,int,int> from, to;
	for (pair<tuple<int,int,int>, tuple<int,int,int>> edge : edges) {
		from = edge.first;
		to = edge.second;
		remove_edge(from, to);
	}
}

bool CDG_E::validate_topo_order() {
    // Validate all edges respect topo order
    for (int u = 0; u < _n_vertices; ++u) {
        const vector<int>& neighbors = _adj_list[u];
        for (size_t i = 0; i < neighbors.size(); ++i) {
            int v = neighbors[i];
            if (_topo_order[u] >= _topo_order[v]) {
                return false;
            }
        }
    }
    return true;
}

bool CDG_E::is_cyclic() {
	// If the topological order is valid, then the graph is acyclic.
	// If the topological order is invalid, we compute a valid topological order (crashed if the graph is cyclic)
	// If computing a valid topological order succeeds, then the graph is acyclic.
	if (!validate_topo_order()) {
		try {
			recompute_topo_order();
		} catch (const runtime_error& e) {
			return true;
		}
	}
	return false;
}

int CDG_E::get_edge_count() const {
	int count = 0;
	for (int u = 0; u < _n_vertices; ++u) {
		count += static_cast<int>(_adj_list[u].size());
	}
	return count;
}
