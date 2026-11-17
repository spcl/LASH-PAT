# Scaling factors from 45nm (what Orion reports) to 7nm (target technology)
area_scaling_factor_45nm_to_7nm = 0.0271        # Area scaling factor from 45nm to 7nm (based on DeepScaleTool #600)
area_scaling_factor_45nm_to_7nm_sram = 0.2      # Area scaling factor for SRAM from 45nm to 7nm (Assumption)
power_scaling_factor_45nm_to_7nm = 0.2473       # Power scaling factor from 45nm to 7nm (based on DeepScaleTool #600)

# For link power calculations
energy_per_bit_per_mm_7nm_in_pj = 0.1           # Assumes one flipflop every 2mm. Need to change this if the signal_propagation_per_cycle_mm changes. Based on paper #605, Checked with LB

# For fault insertion
fault_attempts = 10                             # Number of attempts to insert a fault before giving up (might fail because it disconnects the network or because an already faulty component is selected)
fault_random_seed = 12345                       # Seed for random fault insertion, needs to be the same across experiments to ensure different routing strategies see the same faults, unfair otherwise

# Parameters for computing confidence intervals
ci_num_bootstrap_samples = 1000
ci_confidence_level = 0.95


# Plotting configuration
plot_format = "pdf"
plot_dpi = 300

# BookSim configuration that deeds on trace vs traffic mode
traffic_sample_period = 20000                                   # 20k for traffic mode
trace_sample_period = 1000000                                   # 1M for trace mode

# Default values for BookSim: Adjust these defaults as needed
# The following BookSim parameters need to be set through the experiment configuration:
# routing_function, mod_routing_function, mod_selection_function, traffic, num_vcs, vc_buf_size, packet_size, message_size, in_order_ratio
default_booksim_config = {
    "repetitions" : 5,                                          # Increase for final experiments
    "deadlock_attempts" : 1,                                    # Number of attempts to recover from deadlock 
    "seed" : "time",                                            # Use "time" as seed, otherwise, all reps are identical
    "ignore_cycles": 1,                                         # We simulate compute time in BS, no external cycles needed
    "trace_time_out" : 2 * 60 * 60,                             # Timeout for trace simulations 2 hours per run    
    "precision": 0.001,                                         # 3 decimal places of precision throughout is enough
    "saturation_factor": 2,                                     # Run until latency is 2x the minimum latency
    "router_latency": 4,                                        # Router latency in cycles
    "warmup_periods": 1,                                        # Number of warmup periods
    "sim_count": 1,                                             # Number of simulation periods (we do repetitions outside of BookSim). Link power computation assumes sim_count = 1
    "hold_switch_for_packet": 0,                                # 
    "vc_allocator": "separable_input_first",                    # 
    "sw_allocator": "separable_input_first",                    #
    "alloc_iters": 1,                                           #
    "wait_for_tail_credit": 0,                                  #
    "priority": "none",                                         #
    "injection_rate_uses_flits": 1,                             # Link power computation assumes injection rate is in flits/cycle
    "deadlock_warn_timeout": 200000,                            # 10x the length of the max sample period
    "time_limit" : 12 * 60 * 60,                                # If a single run takes longer than 12h, we abort
    "latency_thres" : 5000.0,                                   # High latency threshold for large packets or high-latency designs. 
}

# Colors for plotting
color_ours = "#55AAAA"
color_multi = "#AA55AA"
color_det = "#AAAA55"
color_lash = "#5555AA"

colors = ["#5A8FC6", "#D96B6B", "#63B883", "#E39A4F", "#A87FB8", "#5FB3AD", "#D6C25A", "#9A6F5A", "#E889A2", "#000000", "#E6B06A", "#3F7F4F", "#B59B4E", "#C9A6D1", "#8FBFE0", "#B5B5B5", "#7A7A7A", "#4F4F4F", "#2B2B2B"]

markers = ["o", "s", "D", "^", "v", "<", ">", "P", "X", "*", "h", "H", "d", "p", "8", "|", "_"]

# Acronyms used for plotting
# Order of keys determines the order within heatmap plots
acronyms = {
    "lash_pat_cost_function_base" : {
        "length" : "Len",
        "layer" : "Lay",
        "phys_channel" : "PC",
        "virt_channel" : "VC",
        "comb_channel" : "CC",
        "cdg" : "CDG",
        "all" : "All",
        "red_all" : "RAll",
    },
    "lash_pat_cost_function_multipath" : {
        "length" : "Len",
        "layer" : "Lay",
        "phys_channel" : "PC",
        "virt_channel" : "VC",
        "comb_channel" : "CC",
        "cdg" : "CDG",
        "all" : "All",
        "red_all" : "RAll",
    },
    "lash_pat_cost_aggregation" : {
        "sum" : "SUM",
        "max" : "MAX",
    },
    "routing_function" : {
        "multi_routing" : "Multi",
        "modular_routing" : None,
    },
    "mod_routing_function" : {
        "xy" : " XY",
        "prefix" : "PREFIX",
        "simple_cycle_breaking_set" : "SCB",
        "lturn" : " LT",
        "up_down" : " UD",
        "left_right" : " LR",
        "lash" : "LASH",
        "a_lash" : "A-LASH",
        "dfsssp" : "DFSSSP",
        "lash_tor" : "LASH-TOR",
        "lash_pat" : "LASH-PAT",
        "dfsssp" : "DFSSSP",
        "a_lash" : "A-LASH",
    },
    "mod_selection_function" : {
        "random" : "  RND",
        "deterministic" : "  DET",
        "adaptive" : "  ADP",
        "hashed" : "  ISF",               # In-order Selective Function
        "adaptive_hashed" : "PAISF",    # Partially Adaptive In-order Selective Function
    },
    "topology" : {
        "mesh" : "M",
        "torus" : "T",
        "folded_torus" : "FT",
        "hypercube" : "HC",
        "flattened_butterfly" : "FBF",
        "hexamesh" : "HM",
        "foldedhexatorus" : "FHT",
        "double_butterfly" : "DBF",
        "butterdonut" : "BD",
        "kite_small" : "KS",
        "kite_medium" : "KM",
        "kite_large" : "KL",
        "wsi_mesh" : "ML",
        "wsi_aligned" : "A",
        "wsi_interleaved" : "I",
        "wsi_rotated" : "R",
    },
    "traffic" : {
        "uniform" : "U",
        "randperm" : "RP",
        "neighbor" : "N",
        "tornado" : "T",
        "asymmetric" : "A",
        "trace_netrace_blackscholes-64" : "CPU",
        "trace_atlahs_llama7B-64" : "GPU",
    },
     "setting" : {
        "noc" : "NoC",
        "pici" : "ICI-P",
        "aici" : "ICI-A",
        "wsi" : "WSI",
    },
}

full_names = {
     "setting" : {
        "noc" : "NoC (Mesh)",
        "pici" : "Passive ICI (HexaMesh)",
        "aici" : "Active ICI (Kite Large)",
        "wsi" : "WSI (Interleaved)",
    },      
    "parameters" : {
        "in_order_ratio" : "In-Order Ratio",
        "n_entropy_bits" : "Entropy Bits",
        "message_size" : "Message Size [packets]",
        "link_fault_rate" : "Link Fault Rate",
        "router_fault_rate" : "Router Fault Rate",
        "n_routers" : "Number of Endpoints",
        "num_vcs" : "Number of Virtual Channels",
        "routing_function" : "Middle Layer",
        "mod_routing_function" : "Routing Function",
        "mod_selection_function" : "Selection",
        "topology" : "Topology",
        "traffic" : "Traffic Pattern",
        "setting" : "Domain",
        "lash_pat_cost_function_base" : "Cost Function B",
        "lash_pat_cost_function_multipath" : "Cost Function MP",
        "lash_pat_n_additional_paths" : "Number of Additional Paths",
        "n_layers" : "Layers",
        "lash_pat_existing_path_bias" : "Existing Path Bias",
        "lash_pat_cost_aggregation" : "Cost Aggregation",
    },
    "topology" : {
        "mesh" : "Mesh",
        "torus" : "Torus",
        "folded_torus" : "Folded Torus",
        "hypercube" : "Hypercube",
        "flattened_butterfly" : "Flattened Butterfly",
        "hexamesh" : "HexaMesh",
        "foldedhexatorus" : "FoldedHexaTorus",
        "double_butterfly" : "Double Butterfly",
        "butterdonut" : "ButterDonut",
        "kite_small" : "Kite Small",
        "kite_medium" : "Kite Medium",
        "kite_large" : "Kite Large",
        "wsi_mesh" : "WSI Mesh",
        "wsi_aligned" : "WSI Aligned",
        "wsi_interleaved" : "WSI Interleaved",
        "wsi_rotated" : "WSI Rotated",
    },
    "mod_selection_function" : {
        "adaptive" : "Adaptive",
    },
    "mod_routing_function" : {
        "up_down" : "Up*/Down*",
        "lash" : "LASH",
        "a_lash" : "A-LASH",
        "dfsssp" : "DFSSSP",
        "lash_tor" : "LASH-TOR",
        "lash_pat" : "LASH-PAT",
        "xy" : "XY",
        "prefix" : "Prefix",
        "simple_cycle_breaking_set" : "SCB",
        "lturn" : "L-Turn",
        "left_right" : "Left/Right",
    },
    "lash_pat_cost_function_multipath" : {
        "length" : "Len",
        "layer" : "Lay",
        "phys_channel" : "PC",
        "virt_channel" : "VC",
        "comb_channel" : "CC",
        "cdg" : "CDG",
        "all" : "All",
        "red_all" : "RAll",
    },
}

orders = {
    "topology" : ["mesh", "torus", "folded_torus", "hypercube", "hexamesh", "foldedhexatorus", "double_butterfly", "butterdonut", "kite_small", "kite_large"],
    "mod_routing_function" : ["prefix", "up_down", "left_right", "lturn", "simple_cycle_breaking_set", "lash", "a_lash", "dfsssp", "lash_tor", "lash_pat","xy"],
    "mod_selection_function" : ["random", "deterministic", "adaptive", "hashed", "adaptive_hashed"],
    "traffic" : ["uniform", "randperm", "neighbor", "tornado", "asymmetric", "trace_netrace_blackscholes-64", "trace_atlahs_llama7B-64"],
    "setting" : ["noc", "pici", "aici", "wsi"],
    "lash_pat_cost_function_base" : ["length", "layer", "phys_channel", "virt_channel", "comb_channel", "cdg", "all", "red_all"],
    "lash_pat_cost_function_multipath" : ["length", "layer", "phys_channel", "virt_channel", "comb_channel", "cdg", "all", "red_all"],
    }
