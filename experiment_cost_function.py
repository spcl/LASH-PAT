# Python modules
import sys
import math
import copy

# Custom modules
import helpers as hlp
import insert_parameters as ip

# Common parameters
common_parameters = {
    'nodes_per_router': 1,                                                  # Number of nodes connected to each router
    "num_vcs": 4,                                                           # Number of virtual channels, we use 4 as a common default for NoCs
    "n_layers" : 4,                                                         # Same as number of VCs.
    "vc_buf_size": 8,                                                       # Buffer depth, we use 8 flits as a common default for NoCs
    "packet_size": 4,                                                       # Number of flits per packet
    "message_size" : 1,                                                     # Number of packets per message
    "in_order_ratio" : 0.0,                                                 # Ratio of messages that require in-order delivery
    "frequency" : 1e9,                                                      # 1 GHz, common in NoCs
    "link_fault_rate" : 0.0,                                                # Probability of link fault
    "router_fault_rate" : 0.0,                                              # Probability of router fault
    "n_entropy_bits" : 8,                                                   # Number of entropy bits hash-based routing 
    "lash_pat_existing_path_bias" : 0.0,
    "lash_pat_cost_aggregation" : "sum",
    "routing_function" : "modular_routing",
    "mod_routing_function" : "lash_pat",
    "mod_selection_function" : "adaptive",
    "fallback_routing_function" : "simple_cycle_breaking_set",
    "lash_pat_cost_function_base" : "cdg",
}

experiments = []

# This code is automatically run when this file is imported
# It composes all experiments of this experiment sweep

# TRAFFIC
for traffic in ["uniform","randperm"]:
    # TOPOLOGY
    for topology in ["mesh","torus","folded_torus","hexamesh","foldedhexatorus","hypercube","double_butterfly","butterdonut","kite_small","kite_large"]:
        # N is set based on the topology
        n_routers = 61 if "hexa" in topology else 64
        # COST FUNCTION
        for cost_function_multipath in ["length","cdg","phys_channel","virt_channel","all"]:
            # Experiment Settings
            exp = copy.deepcopy(common_parameters)
            ip.insert_technology_dependent_parameters(exp, topology)
            exp['traffic'] = traffic
            exp['topology'] = topology
            exp['n_routers'] = n_routers
            exp["lash_pat_n_additional_paths"] = ip.get_lash_pat_n_additional_paths(topology)
            exp['lash_pat_cost_function_multipath'] = cost_function_multipath
            # NOTE: It must be manually ensured that there are no conflicts in experiment names across different experiment sweep files and experiments within a sweep
            #       If this is not the case, results may be overwritten and lost!
            exp['name'] = f"cost_function_{traffic}_{topology}_{cost_function_multipath}"
            # NOTE: The list of varied parameters must match the order in which they appear in the experiment name (see above)
            exp['vary'] = ["traffic","topology","cost_function_multipath"]
            # Settings for plotting, the order in these lists determines the arrangement in the heatmap grid
            exp['plot_type'] = 'heatmap'
            exp['plot_cols'] = ['topology','traffic']
            exp['plot_rows'] = ['lash_pat_cost_function_multipath']
            experiments.append(exp)
