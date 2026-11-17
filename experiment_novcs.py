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
    "num_vcs": 1,                                                           # Number of virtual channels, we use 4 as a common default for NoCs
    "n_layers" : 1,                                                         # Same as number of VCs.
    "vc_buf_size": 8,                                                       # Buffer depth, we use 8 flits as a common default for NoCs
    "packet_size": 4,                                                       # Number of flits per packet
    "message_size" : 1,                                                     # Number of packets per message
    "in_order_ratio" : 0.0,                                                 # Ratio of messages that require in-order delivery
    "frequency" : 1e9,                                                      # 1 GHz, common in NoCs
    "link_fault_rate" : 0.0,                                                # Probability of link fault
    "router_fault_rate" : 0.0,                                              # Probability of router fault
    "n_entropy_bits" : 8,                                                   # Number of entropy bits hash-based routing 
    "lash_pat_existing_path_bias" : 0.0,
    "lash_pat_cost_function_base" : "cdg",
    "lash_pat_cost_function_multipath" : "all",
    "lash_pat_cost_aggregation" : "sum",
    "routing_function" : "modular_routing",
    "mod_selection_function" : "adaptive",
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
        mod_routing_functions = ["lash_pat","simple_cycle_breaking_set","up_down","prefix","left_right","lash","dfsssp","lash_tor","a_lash","lturn"]
        # ROUTING FUNCTION
        for mod_routing_function in mod_routing_functions:
            if mod_routing_function in ["lash_tor"]:
                fallback_routing_functions = "up_down"
            elif mod_routing_function in ["lash_pat"]:
                if topology in ["torus","foldedhexatorus","double_butterfly","kite_large","kite_small"]:
                    fallback_routing_functions = "simple_cycle_breaking_set"
                elif topology in ["mesh","folded_torus","hexamesh","hypercube","butterdonut"]:
                    fallback_routing_functions = "lturn"
                else:
                    print(f"Warning in experiment_novcs.py: No fallback routing function defined for topology {topology} with routing function {mod_routing_function}.")
            else:
                fallback_routing_functions = "none"

            fallback_routing_functions = "up_down" if mod_routing_function in ["lash_tor"] else ("simple_cycle_breaking_set" if mod_routing_function in ["lash_pat"] else "none")
            # Experiment Settings
            exp = copy.deepcopy(common_parameters)
            ip.insert_technology_dependent_parameters(exp, topology)
            exp['traffic'] = traffic
            exp['topology'] = topology
            exp['n_routers'] = n_routers
            exp['mod_routing_function'] = mod_routing_function
            exp['fallback_routing_function'] = fallback_routing_functions
            exp["lash_pat_n_additional_paths"] = ip.get_lash_pat_n_additional_paths(topology)
            # NOTE: It must be manually ensured that there are no conflicts in experiment names across different experiment sweep files and experiments within a sweep
            #       If this is not the case, results may be overwritten and lost!
            exp['name'] = f"novcs_{traffic}_{topology}_{mod_routing_function}"
            # NOTE: The list of varied parameters must match the order in which they appear in the experiment name (see above)
            exp['vary'] = ["traffic","topology","mod_routing_function"]
            # Settings for plotting, the order in these lists determines the arrangement in the heatmap grid
            exp['plot_type'] = 'heatmap'
            exp['plot_cols'] = ['topology','traffic']
            exp['plot_rows'] = ['mod_routing_function']
            experiments.append(exp)
