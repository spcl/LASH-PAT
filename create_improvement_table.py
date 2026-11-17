# Python modules
import sys
import copy
from types import SimpleNamespace
import numpy as np
from collections import Counter


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

# Helper function to extract the relevant metrics from the results

def extract_metrics(res_raw, metrics):
    # Return None of there are no results
    if len(res_raw) == 0:
        return None
    # Return None if no non-deadlocked runs exist
    res_no_dl = [r for r in res_raw if not any(r["booksim_details"][key].get("number_of_deadlocked_runs", -1) >= r["booksim_config"]["deadlock_attempts"] for key in r["booksim_details"])]
    if len(res_no_dl) == 0:
        return None
    # Return None if the majority of runs had insufficient VCs
    all_statuses = [r["summary"]["status"] for r in res_no_dl]
    majority_status = Counter(all_statuses).most_common(1)[0][0] if len(all_statuses) > 0 else None
    res_valid = [r for r in res_no_dl if r["summary"]["status"] == "success"]
    if majority_status == "Insufficient VCs":
        return None
    res = res_valid
    output = {}
    for metric in metrics:
        if metric == "throughput":
            # Throughput
            throughput = hlp.avg_harmonic([res[i]["summary"]["saturation_throughput"] for i in range(len(res))])
            output["throughput"] = throughput
        elif metric == "zero_load_latency":
            # Zero load latency
            zero_load_latency = hlp.avg_arithmetic([res[i]["summary"]["zero_load_latency"] for i in range(len(res))])
            output["zero_load_latency"] = zero_load_latency
        elif metric == "latency_under_load":
            # Latency under load (set to none if the majority of runs had insufficient throughput)
            res_suf_tp = [r for r in res_valid if r["summary"]["status"] == "success" and "saturation_throughput" in r["summary"] and r["summary"]["saturation_throughput"] >= 0.1]
            lateny_under_load = hlp.avg_arithmetic([res[i]["summary"]["zero_load_latency"] for i in range(len(res_suf_tp))]) if len(res_suf_tp) >= (len(res_valid) / 2) else None
            output["latency_under_load"] = lateny_under_load
        elif metric == "energy":
            # Energy
            energy = hlp.avg_harmonic([res[i]["summary"]["energy_per_packet"] for i in range(len(res))])
            output["energy"] = energy
        elif metric == "average_latency":
            # Average latency
            average_latency = hlp.avg_arithmetic([res[i]["summary"]["average_latency"] for i in range(len(res))])
            output["average_latency"] = average_latency
    return output

def create_improvement_table(experiment_sweep, sweep_name, metrics):

    # Identify unique topologies and traffic patterns in the experiment sweep
    topology_set = set()
    traffic_set = set()
    routing_set = set()

    # Read all results from the experiment sweep
    all_results = hlp.read_results(experiment_sweep.experiments)

    for experiment in experiment_sweep.experiments:
        exp = SimpleNamespace(**experiment)
        if exp.topology not in topology_set:
            topology_set.add(exp.topology)
        if exp.traffic not in traffic_set:
            traffic_set.add(exp.traffic)
        if exp.mod_routing_function not in routing_set:
            routing_set.add(exp.mod_routing_function)

    relative_metrics = {rf : {metric : [] for metric in metrics} for rf in routing_set if rf != "lash_pat"}
    relative_metrics["Best"] = {metric : [] for metric in metrics}

    # Iterate through topologies
    for topology in sorted(list(topology_set), key = lambda x : cfg.orders["topology"].index(x) if x in cfg.orders["topology"] else len(cfg.orders["topology"])):
        # Iterate through traffic patterns
        for traffic in sorted(list(traffic_set), key = lambda x : cfg.orders["traffic"].index(x) if x in cfg.orders["traffic"] else len(cfg.orders["traffic"])):
            exp_name = f"{sweep_name}_{traffic}_{topology}_lash_pat"
            lp_results = all_results.get(exp_name, None)
            if lp_results is None:
                print(f"Warning: No results found for experiment {exp_name}. Skipping.")
                continue
            lp_metrics = extract_metrics(lp_results, metrics)
            if lp_metrics is None:
                continue
            # Iterate through routing functions to compare to
            higher_is_better_metrics = ["throughput"]
            best = {metric : float("inf") if metric in higher_is_better_metrics else -float("inf") for metric in metrics}
            for routing in sorted(list(routing_set), key = lambda x : cfg.orders["mod_routing_function"].index(x) if x in cfg.orders["mod_routing_function"] else len(cfg.orders["mod_routing_function"])):
                # Skip the comparison if the routing function is the same as the baseline
                if routing == "lash_pat":
                    continue
                exp_name = f"{sweep_name}_{traffic}_{topology}_{routing}"
                rf_results = all_results.get(exp_name, None)
                if rf_results is None:
                    print(f"Warning: No results found for experiment {exp_name}. Skipping.")
                    continue
                rf_metrics = extract_metrics(rf_results, metrics)
                if rf_metrics is None:
                    print(f"Warning: No valid metrics found for experiment {exp_name}. Skipping.")
                    continue
                # Compute relative improvements
                for metric in metrics:
                    lp_value = lp_metrics[metric]
                    rf_value = rf_metrics[metric]
                    if lp_value is None or rf_value is None:
                        continue
                    # Compute how many percent lp_value is better than rf_value
                    improvement = (lp_value - rf_value) / rf_value * 100
                    relative_metrics[routing][metric].append(improvement)
                    # For the "best baseline", we just store the worst improvement.
                    best[metric] = min(best[metric], improvement) if metric in higher_is_better_metrics else max(best[metric], improvement)
            # Add the best metrics to the relative_metrics dictionary
            for metric in metrics:
                if best[metric] is not None and best[metric] != float("inf") and best[metric] != -float("inf"):
                    relative_metrics["Best"][metric].append(best[metric])

    # Compute average improvements for each routing function
    average_improvements = {}
    for routing in relative_metrics:
        average_improvements[routing] = {}
        for metric in relative_metrics[routing]:
            values = relative_metrics[routing][metric]
            if len(values) == 0:
                average_improvements[routing][metric] = None
            else:
                sign = np.sign(np.mean(values))
                absolute_values = [abs(v) for v in values]
                average_improvements[routing][metric] = hlp.avg_geometric(absolute_values) * sign


    # Create a LaTeX table of the average improvements 
    table_rows = []
    table_rows.append(f"{'Routing Function':<10}" + " & ".join([f"{metric.replace('_', ' ').title():<10}" for metric in ["throughput", "zero_load_latency", "latency_under_load", "energy"]]) + " \\\\")
    for routing in sorted(average_improvements.keys(), key = lambda x : cfg.orders["mod_routing_function"].index(x) if x in cfg.orders["mod_routing_function"] else len(cfg.orders["mod_routing_function"])):
        routing_name = cfg.full_names["mod_routing_function"].get(routing, routing)
        row = f"{routing_name:<10} & "
        for metric in metrics:
            value = average_improvements[routing][metric]
            if value is None:
                row += f"{'N/A':<10} & "
            else:
                sign = "+" if value >= 0 else "-"
                round_value = str(abs(round(value, 2))) + "%"
                row += f"{sign}{round_value:<9} & "
        row = row[:-3] + " \\\\"
        table_rows.append(row)

    # Print the LaTeX table to console
    print("\n".join(table_rows))


# Synthetic traffic
experiment_sweep = experiment_64cores
sweep_name = "64cores"
metrics = ["throughput", "zero_load_latency", "latency_under_load", "energy"]
create_improvement_table(experiment_sweep, sweep_name, metrics)

# Traffic traces
experiment_sweep = experiment_trace
sweep_name = "trace"
metrics = ["average_latency"]
create_improvement_table(experiment_sweep, sweep_name, metrics)



