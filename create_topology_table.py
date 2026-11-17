# Python modules
import sys
import copy
from types import SimpleNamespace

# Custom modules
import helpers as hlp
import config as cfg
import generate_network as gn

# Import experiment sweeps
import experiment_64cores
import experiment_n_additional_paths
import experiment_n_layers
import experiment_vary_vcs
import experiment_cost_function
import experiment_trace
import experiment_novcs

experiment_sweeps = [
    experiment_64cores.experiments,
    experiment_n_additional_paths.experiments,
    experiment_n_layers.experiments,
    experiment_vary_vcs.experiments,
    experiment_cost_function.experiments,
    experiment_trace.experiments,
    experiment_novcs.experiments,
]

topology_set = set()
topologies = []

for experiment in hlp.flatten_list(experiment_sweeps):
    exp = SimpleNamespace(**experiment)
    if exp.topology not in topology_set:
        topology_set.add(exp.topology)
        network = gn.generate_network(exp.n_routers, exp.topology, exp.nodes_per_router, exp.link_latency_function, exp.node_link_latency, exp.width, exp.height)
        nw = SimpleNamespace(**network)
        n_routers = len(nw.adj_list)
        n_endpoints = sum(nw.node_counts)
        is_direct = min(nw.node_counts) == max(nw.node_counts)
        diameter = hlp.compute_network_diameter(nw.adj_list)
        min_radix = min(len(x) for x in nw.adj_list)
        max_radix = max(len(x) for x in nw.adj_list)
        radix = str(max_radix) if min_radix == max_radix else f"{min_radix}-{max_radix}"
        topo = {"topology" : exp.topology, "n_routers" : n_routers, "n_endpoints" : n_endpoints, "is_direct" : is_direct, "diameter" : diameter, "radix" : radix}
        topologies.append(topo)


# Nicely print a table of the topologies
print(f"{'Topology':<30} & {'# Routers':<10} & {'Diameter':<10} & {'Radix':<10} \\\\")
for topo in topologies:
    short = cfg.acronyms["topology"][topo["topology"]]
    full_name = "%s (%s)" % (cfg.full_names["topology"][topo["topology"]], short)
    print(f"{full_name:<30} & {topo['n_routers']:<10} & {topo['diameter']:<10} & {topo['radix']:<10} \\\\")


