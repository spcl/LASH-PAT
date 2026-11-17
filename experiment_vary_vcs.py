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
    "vc_buf_size": 8,                                                       # Buffer depth, we use 8 flits as a common default for NoCs
    "packet_size": 4,                                                       # Number of flits per packet
    "message_size" : 1,                                                     # Number of packets per message
    "in_order_ratio" : 0.0,                                                 # Ratio of messages that require in-order delivery
    "frequency" : 1e9,                                                      # 1 GHz, common in NoCs
    "link_fault_rate" : 0.0,                                                # Probability of link fault
    "router_fault_rate" : 0.0,                                              # Probability of router fault
    "n_entropy_bits" : 8,                                                   # Number of entropy bits hash-based routing 
    "lash_pat_existing_path_bias" : 0.1,
    "lash_pat_cost_function_base" : "cdg",
    "lash_pat_cost_function_multipath" : "all",
    "lash_pat_cost_aggregation" : "sum",
    "routing_function" : "modular_routing",
    "mod_selection_function" : "adaptive",
    "traffic" : "randperm",
}

experiments = []

# This code is automatically run when this file is imported
# It composes all experiments of this experiment sweep

# N is set based on the topology
mod_routing_functions = ["lash_pat","simple_cycle_breaking_set","up_down","prefix","left_right","lash","dfsssp","lash_tor","a_lash","lturn"]
for mod_routing_function in mod_routing_functions:
    fallback_routing_functions = "up_down" if mod_routing_function in ["lash_tor"] else ("simple_cycle_breaking_set" if mod_routing_function in ["lash_pat"] else "none")
    # Varying the number of virtual channels as the main parameter of this experiment.
    for num_vcs in [1,2,4,8]:
        # Also vary  the network topology
        for topology in ["folded_torus","hexamesh","butterdonut"]:
            # N is set based on the topology
            n_routers = 61 if "hexa" in topology else 64
            # Experiment Settings
            exp = copy.deepcopy(common_parameters)
            ip.insert_technology_dependent_parameters(exp, topology)
            exp['num_vcs'] = num_vcs
            exp['n_layers'] = min(num_vcs,4) if mod_routing_function in ["lash_pat"] else (1 if mod_routing_function in ["lash_tor"] else -1)
            exp['topology'] = topology
            exp['n_routers'] = n_routers
            exp['mod_routing_function'] = mod_routing_function
            exp['fallback_routing_function'] = fallback_routing_functions
            exp["lash_pat_n_additional_paths"] = ip.get_lash_pat_n_additional_paths(topology)
            # NOTE: It must be manually ensured that there are no conflicts in experiment names across different experiment sweep files and experiments within a sweep
            #       If this is not the case, results may be overwritten and lost!
            exp['name'] = f"vary_vcs_{topology}_{mod_routing_function}_{num_vcs}"
            # NOTE: The list of varied parameters must match the order in which they appear in the experiment name (see above)
            exp['vary'] = ["topology","mod_routing_function","num_vcs"]
            # Settings for plotting, the order in these lists determines the arrangement in the heatmap grid
            exp['plot_type'] = 'lines'
            exp['plot_subplot'] = 'topology'
            exp['plot_x_axis'] = 'num_vcs'
            exp['plot_lines'] = ['mod_routing_function']
            experiments.append(exp)
