import pymetis
import random

def compute_upper_bound_on_throughput(adj_list, node_counts):
    # Settings
    n_runs = 100
    base_seed = 1
    # Parameters
    n_routers = len(adj_list)
    n_nodes = sum(node_counts)
    # Compute best cut using PyMetis
    best_cut = float("inf")
    worst_cut = 0
    best_part = None
    for i in range(n_runs):
        seed = base_seed + i
        random.seed(seed)
        n_cuts, membership = pymetis.part_graph(2,adjacency=adj_list)
        if n_cuts < best_cut:
            best_cut = n_cuts
            best_part = membership
        if n_cuts > worst_cut:
            worst_cut = n_cuts
    nodes_in_part_1 = sum(node_counts[i] for i in range(n_routers) if best_part[i] == 0)
    nodes_in_part_2 = sum(node_counts[i] for i in range(n_routers) if best_part[i] == 1)
    # Sanity check
    if nodes_in_part_1 + nodes_in_part_2 != n_nodes:
        raise ValueError("Partitioning error: total nodes in partitions do not match total nodes in graph.")
    # Transform Bisection Bandwidth to Throughput bound
    traffic_per_src_dst_pair = 1.0 / n_nodes
    traffic_across_cut = nodes_in_part_1 * nodes_in_part_2 * traffic_per_src_dst_pair
    bound_on_throughput = best_cut / traffic_across_cut
    # Additionally, the throughput is bound by the injection rate, which cannot be higher than 1.0
    bound_on_throughput = min(bound_on_throughput, 1.0)
    return bound_on_throughput

def compute_lower_bound_on_latency(adj_list, latencies, node_counts, node_link_latency, router_latency):
    # For each source-destination pair, we find the shortest path using Dijkstra's algorithm and sum the latencies along that path (nodes and edges).
    n_routers = len(adj_list)
    n_nodes = sum(node_counts)
    # Create a mapping from node index to router index
    node_to_router = []
    for router_index, count in enumerate(node_counts):
        node_to_router.extend([router_index] * count)
    # Compute shortest paths using Dijkstra's algorithm
    all_pairs_latencies = []
    for src_nid in range(n_nodes):
        for dst_nid in range(n_nodes):
            if src_nid == dst_nid:
                all_pairs_latencies.append(node_link_latency + router_latency + node_link_latency)
            # Dijkstra's algorithm
            src_router = node_to_router[src_nid]
            dst_router = node_to_router[dst_nid]
            visited = [False] * n_routers
            min_heap = [(node_link_latency, src_router)]  # (cumulative_latency, router_index)
            while min_heap:
                cumulative_latency, current_router = min_heap.pop(0)
                if visited[current_router]:
                    continue
                visited[current_router] = True
                if current_router == dst_router:
                    # Found the destination router, compute total latency
                    total_latency = cumulative_latency + router_latency + node_link_latency
                    all_pairs_latencies.append(total_latency)
                    break
                for idx, neighbor in enumerate(adj_list[current_router]):
                    if not visited[neighbor]:
                        edge_latency = latencies[current_router][idx]
                        min_heap.append((cumulative_latency + router_latency + edge_latency, neighbor))
                        min_heap.sort()
    bound_on_latency = sum(all_pairs_latencies) / len(all_pairs_latencies)
    return bound_on_latency


def compute_performance_bounds(adj_list, latencies, node_counts, node_link_latency, router_latency):
    results = {}
    results["saturation_throughput"] = compute_upper_bound_on_throughput(adj_list, node_counts)
    results["zero_load_latency"] = compute_lower_bound_on_latency(adj_list, latencies, node_counts, node_link_latency, router_latency)
    return results
