# Import python modules
import copy
import matplotlib.colors as mcolors
import sys
import os
import math
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.colors import ListedColormap, BoundaryNorm
from matplotlib.ticker import MaxNLocator
from types import SimpleNamespace
from collections import Counter
from matplotlib import cm
import numpy as np


# Import custom modules
import config as cfg
import helpers as hlp
import generate_network as gn
from main import experiment_sweeps
import fault_insertion as fi

def plot_improvement_bar_chart(experiments : list[dict], heatmap_data : tuple, name : str, metric : str, vs : str = "us_vs_baseline") -> None:
    (data_matrix, row_combos, col_combos) = heatmap_data
    # Skip if no data
    if data_matrix is None:
        return
    # Skip if either all or none of the data is from our method (improvement plot makes no sense in that case)
    if "mod_routing_function" not in dict(row_combos[0]):
        return
    # Setting based on metric
    if metric in ["saturation_throughput", "average_throughput"]:
        best_function = np.nanmax
        better_comp = lambda x, y : x >= y
    elif metric in ["latency_at_0.1","zero_load_latency","energy_per_packet","total_power", "average_latency"]:
        best_function = lambda l : np.nanmin([x for x in l if not math.isnan(x) and x >= 0.0])
        better_comp = lambda x, y : x <= y
    else:
        hlp.print_red("Error: Unknown metric \"%s\" for improvement bar chart." % metric)
        return
    # Compute best our and best baseline per column
    relative_values = []
    baselines = []
    for col_idx, col_combo in enumerate(col_combos):
        best_baseline_val = float("nan")
        best_baseline_nam = ""
        best_ours_val = float("nan")
        for row_idx, row_combo in enumerate(row_combos):
            if vs == "us_vs_baseline":
                ours_condition = "lash_pat" in dict(row_combo)["mod_routing_function"]
            else:
                hlp.print_red("Error: Unknown vs condition \"%s\" for improvement bar chart." % vs)
                return
            if ours_condition:
                if not math.isnan(data_matrix[row_idx, col_idx]) and data_matrix[row_idx, col_idx] >= 0 and (math.isnan(best_ours_val) or best_function([best_ours_val, data_matrix[row_idx, col_idx]]) == data_matrix[row_idx, col_idx]):
                    best_ours_val = data_matrix[row_idx, col_idx]
            else:
                if not math.isnan(data_matrix[row_idx, col_idx]) and data_matrix[row_idx, col_idx] >= 0 and (math.isnan(best_baseline_val) or best_function([best_baseline_val, data_matrix[row_idx, col_idx]]) == data_matrix[row_idx, col_idx]):
                    best_baseline_val = data_matrix[row_idx, col_idx]
                    best_baseline_nam = " | ".join(cfg.acronyms[opt][x] for opt, x in sorted(dict(row_combo).items(), key=lambda item: experiments[0]["plot_rows"].index(item[0])) if cfg.acronyms[opt][x] is not None)
        relative_value = best_ours_val * 100 / best_baseline_val if not math.isnan(best_ours_val) and not math.isnan(best_baseline_val) and best_baseline_val != 0 else float("nan")
        relative_values.append(relative_value)
        baselines.append(best_baseline_nam.replace(" ",""))
    e0 = experiments[0]
    # Create bar chart
    (height, bottom) = (3.5, 0.375) if "trace" in e0["name"] else (3, 0.25)
    fig, ax = plt.subplots(1,1, figsize=(4,height))
    fig.subplots_adjust(top = 0.85, bottom = bottom, left = 0.25, right = 0.99)
    # Plot all bars, positive values in green, negative in red
    for idx, rel_val in enumerate(relative_values):
        color = "#99CC99" if better_comp(rel_val, 100) else "#CC9999"
        ax.bar(idx, rel_val, color=color, zorder=3)
    # Plot horizontal line at 100%
    ax.axhline(100, color="#000000", linestyle="-", linewidth=1.0, zorder=2)
    # Print average improvement in middle of plot
    avg_rel_val = hlp.avg_geometric([x for x in relative_values if not math.isnan(x)]) # Ratio -> Geometric mean
    met_name = metric.replace("_"," ").title().replace("Average ","") # Avoid "Average Relative Average {Metric}"
    met_name = met_name.replace("At 0.1", "Under Load")
    if vs == "us_vs_baseline":
        ax.set_title("Average Relative " + met_name + "\n of LASH-PAT w.r.t. Best Baseline: %.2f%%" % avg_rel_val, fontsize=10)
    # Set ticks and labels
    if max(relative_values) < float("inf"):
        diff = (max(relative_values) - min(relative_values))
        rt = 50 if diff > 100 else (10 if diff > 20 else 5)
        ax.set_ylim((min(relative_values)*0.99)//rt*rt, math.ceil(max(relative_values)*1.01/rt)*rt)
    ax.set_xticks(range(len(col_combos)))
    rot = 60
    ax.set_xticklabels([" | ".join(f"{cfg.acronyms[opt][dict(col_combo)[opt]]}" for opt in experiments[0]["plot_cols"] if cfg.acronyms[opt][dict(col_combo)[opt]] is not None) for col_combo in col_combos], rotation=rot, ha="right", fontsize=8)
    ylab = f"Relative {metric.replace('_',' ').title()} of LASH-PAT w.r.t. Best Baseline [%]"
    ax.set_ylabel(hlp.split_string(ylab, 3), labelpad=1)
    # xlabel move upwards
    ax.set_xlabel(" | ".join([x.capitalize() for x in experiments[0]["plot_cols"]]), labelpad=-1)
    ax.grid(axis='y', color = "#CCCCCC", linewidth=0.5, zorder=0)
    # Save plot
    plt.savefig("plots/improvement_%s_%s.%s" % (vs, name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()


def create_line_plots(experiments : list[dict], name : str, metric : str, do_save = True) -> None:
    color_ours = cfg.color_ours
    color_multi = cfg.color_multi
    color_det = cfg.color_det
    color_lash = cfg.color_lash
    # Settings based on metric
    if metric in ["saturation_throughput", "average_throughput"]:
        best_function = lambda l : np.nanmax(l) if len(l) > 0 and not all(math.isnan(x) for x in l) else float("nan")
        worst_function = lambda l : np.nanmin(l) if len(l) > 0 and not all(math.isnan(x) for x in l) else float("nan")
    elif metric in ["latency_at_0.1","zero_load_latency", "energy_per_packet","total_power", "average_latency"]:
        best_function = lambda l : np.nanmin(l) if len(l) > 0 and not all(math.isnan(x) for x in l) else float("nan")
        worst_function = lambda l : np.nanmax(l) if len(l) > 0 and not all(math.isnan(x) for x in l) else float("nan")
    else:
        hlp.print_red("Error: Unknown metric \"%s\" for line plot." % metric)
        return
    # Load all results of this experiment sweep
    all_results = hlp.read_results(experiments)
    e0 = experiments[0]
    # Skip this plot if no results are available
    if len(all_results) == 0:
        hlp.print_yellow("Warning: No results found for line plot \"%s\". Skipping..." % name)
        return
    hlp.print_cyan("Creating line plot for \"%s\"" % name)
    # Identify subplots
    subplot_values = sorted(list(set([x[x["plot_subplot"]] for x in experiments])), key = lambda x : cfg.orders[e0["plot_subplot"]].index(x) if x in cfg.orders[e0["plot_subplot"]] else x)
    # Create plot
    width = 4 * len(subplot_values)
    left = 0.22 / len(subplot_values)
    fig, ax = plt.subplots(1,len(subplot_values), figsize=(width,3))
    fig.subplots_adjust(top = 0.925, bottom = 0.175, left = left, right = 0.99, wspace=0.15)
    if len(subplot_values) == 1:
        ax = [ax]
    # Iterate through all subplots
    xvalues = []
    for i, subplot_value in enumerate(subplot_values):
        line_data = []
        line_color = []
        line_marker = []
        line_label = []
        # Construct all lines
        line_combos = set()
        for exp in experiments:
            line_combo = {opt : exp[opt] for opt in e0["plot_lines"]}
            line_combos.add(frozenset(line_combo.items()))
        # Iterate through all lines
        for line_combo in line_combos:
            xvalues = sorted(list(set([x[x["plot_x_axis"]] for x in experiments])))
            yvalues = []
            ylower = []
            yupper = []
            for xval in xvalues:
                params = {opt : dict(line_combo)[opt] for opt in e0["plot_lines"]}
                params[e0["plot_subplot"]] = subplot_value
                params[e0["plot_x_axis"]] = xval
                tmp = str(e0["plot_x_axis"])
                tmp_map = {"n_routers" : "nw_size", "link_fault_rate" : "link_faults", "router_fault_rate" : "router_faults", "lash_pat_n_additional_paths" : "n_additional_paths", "lash_pat_existing_path_bias" : "existing_path_bias", "num_vcs" : "vcs"}
                tmp = tmp_map.get(tmp, tmp)
                exp_name = "vary_" + tmp + "_" + "_".join([str(params[opt]) for opt in e0["vary"]])
                if "n_additional_paths" in exp_name or "existing_path_bias" in exp_name or "n_layers" in exp_name:
                    exp_name = exp_name.replace("vary_","")
                result = all_results.get(exp_name, None)
                if result is None:
                    yvalues.append(float("nan"))
                    ylower.append(float("nan"))
                    yupper.append(float("nan"))
                else:
                    values = None
                    mean_func = None
                    if metric in ["latency_at_0.1"]:
                        mean_func = hlp.avg_arithmetic # Cost -> Arithmetic mean
                        values = hlp.identify_latencies_at_load(result, 0.1)
                    elif metric in ["zero_load_latency", "average_latency"]:
                        mean_func = hlp.avg_arithmetic # Cost -> Arithmetic mean
                        values = [result[i]["summary"][metric] for i in range(len(result))]
                    elif metric in ["saturation_throughput", "average_throughput", "energy_per_packet"]:
                        mean_func = hlp.avg_harmonic  # Rate -> Harmonic mean
                        values = [result[i]["summary"][metric] for i in range(len(result))]
                    elif metric == "total_power":
                        mean_func = hlp.avg_arithmetic # Cost -> Arithmetic mean
                        values = [result[i]["summary"]["total_router_power"] + result[i]["summary"]["total_link_power"] for i in range(len(result))]
                    else:
                        hlp.print_red("Error: Unknown metric \"%s\" for line plot." % metric)
                        return
                    mean, lower, upper, rel_half_width = hlp.compute_confidence_interval(values, mean_func)
                    yvalues.append(mean)
                    ylower.append(lower)
                    yupper.append(upper)
            line_data.append((xvalues, yvalues, ylower, yupper))
            # Determine the line color
            if "topology" in dict(line_combo):
                col = cfg.colors[cfg.orders["topology"].index(dict(line_combo)["topology"]) % len(cfg.colors)]
                mar = cfg.markers[cfg.orders["topology"].index(dict(line_combo)["topology"]) % len(cfg.markers)]
                lab = cfg.full_names["topology"][dict(line_combo)["topology"]]
            elif "mod_routing_function" in dict(line_combo):
                col = cfg.colors[cfg.orders["mod_routing_function"].index(dict(line_combo)["mod_routing_function"]) % len(cfg.colors)]
                mar = cfg.markers[cfg.orders["mod_routing_function"].index(dict(line_combo)["mod_routing_function"]) % len(cfg.markers)]
                lab = cfg.full_names["mod_routing_function"][dict(line_combo)["mod_routing_function"]]
            else:
                col = "#000000"
                lab = "???"
                mar = "o"
            line_color.append(col)
            line_marker.append(mar)
            line_label.append(lab)
        # Plot all lines
        for idx, (xvalues, yvalues, ylower, yupper) in enumerate(line_data):
            yerr = [[yvalues[j] - ylower[j] for j in range(len(yvalues))], [yupper[j] - yvalues[j] for j in range(len(yvalues))]] if not all(math.isnan(y) for y in yvalues) else None
            ax[i].errorbar(xvalues, yvalues, yerr=yerr, color=line_color[idx], linewidth=1.0, linestyle="-", marker = line_marker[idx], markersize=3, label=line_label[idx], zorder=3, elinewidth = 1, ecolor = line_color[idx], capsize=2, capthick=0.5)
        # Set labels and title
        if len(subplot_values) > 1:
            ax[i].set_title(cfg.full_names[e0["plot_subplot"]][subplot_value])
        ax[i].set_xlabel(cfg.full_names["parameters"][e0["plot_x_axis"]], fontsize=8 if len(ax) == 1 else 12)
        if i == 0:
            lab = metric.replace("_"," ").title()
            lab = lab + ("\n[flits/endpoint/cycle]" if "Throughput" in lab else "")
            lab = lab + ("\n[nJ/packe]" if "Energy" in lab else "")
            ax[i].set_ylabel(lab, fontsize=8 if len(ax) == 1 else 12)
        # For the VC plot, we use log2 for x-axis
        if e0["plot_x_axis"] == "num_vcs":
            ax[i].set_xscale('log', base=2)
            ax[i].set_xticks(xvalues)
            ax[i].set_xticklabels([str(int(x)) for x in xvalues])
        cur_low, cur_high = ax[i].get_ylim()
        bot_lim = cur_low * 0.99 if ((cur_high - cur_low) < (cur_low * 2)) else 0
        ax[i].set_ylim(bottom=bot_lim, top=cur_high * 1.01)
        ax[i].grid(color = "#CCCCCC", linewidth=0.5, zorder=0)
        # Legend with uniform marker size and line width
        if len(set(line_label)) > 2:
            handles, labels = ax[i].get_legend_handles_labels()
            labels_internal = [lab.replace(" ","_").replace("-","_").lower() for lab in line_label]
            labels_internal = [lab.replace("l_turn","lturn").replace("scb","simple_cycle_breaking_set") for lab in labels_internal]
            order = sorted(range(len(labels)), key=lambda idx: cfg.orders[e0["plot_lines"][0]].index(labels_internal[idx]) if labels_internal[idx] in cfg.orders[e0["plot_lines"][0]] else float("inf"))
            handles = [handles[idx] for idx in order]
            labels = [labels[idx] for idx in order]
            loc = "lower right" if e0["plot_x_axis"] == "lash_pat_n_additional_paths" else "upper left"
            ax[i].legend(handles, labels, loc=loc, fontsize=6 if len(ax) == 1 else 8, ncol=3, framealpha=0.5, handlelength=1.5, handletextpad=0.5, columnspacing=0.5)
        # Set log x-axis for the existing path bias plot
        if e0["plot_x_axis"] == "lash_pat_existing_path_bias":
            ax[i].set_xscale('symlog', linthresh=1e-4)
    # Save plot if not all values are NaN
    if do_save and not all(all(math.isnan(yvalues[idx]) for yvalues, _, _, _ in line_data) for idx in range(len(line_data))):
        plt.savefig("plots/line_plot_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()

# Legend for raw data matrix: >=0 Real value, -10 missing in data, -11 not applicable, -12 deadlocked, -13 insufficient VCs
def create_heatmap_plot(experiments : list[dict], name : str, metric : str, do_save = True) -> None:
    # Config
    digits = int(-math.log10(cfg.default_booksim_config["precision"]))
    # Settings based on metric
    if metric in ["saturation_throughput", "average_throughput"]:
        rank_1_func = np.nanmax
        rank_2_func = lambda l : np.nanmax([float("nan")] + [x for x in l if not math.isnan(x) and x >= 0.0 and x < rank_1_func(l)])
    elif metric in ["latency_at_0.1","zero_load_latency", "energy_per_packet","total_power", "average_latency"]:
        rank_1_func = lambda l : np.nanmin([x for x in l if not math.isnan(x) and x >= 0.0])
        rank_2_func = lambda l : np.nanmin([float("nan")] + [x for x in l if not math.isnan(x) and x >= 0.0 and x > rank_1_func(l)])
    else:
        hlp.print_red("Error: Unknown metric \"%s\" for heatmap plot." % metric)
        return
    # Load all results of this experiment sweep
    all_results = hlp.read_results(experiments)
    # Skip this plot if no results are available
    if len(all_results) == 0:
        hlp.print_yellow("Warning: No results found for heatmap plot \"%s\". Skipping..." % name)
        return (None, None, None)
    hlp.print_cyan("Creating Heatmap plot for \"%s\"" % name)
    # Prepare data gathering
    e0 = experiments[0]
    row_combos = set()
    col_combos = set()
    for exp in experiments:
        row_combo = {opt : exp[opt] for opt in e0["plot_rows"]}
        col_combo = {opt : exp[opt] for opt in e0["plot_cols"]}
        row_combos.add(frozenset(row_combo.items()))
        col_combos.add(frozenset(col_combo.items()))
    # Sort rows and columns based on acronym order
    order = {opt : {key : idx for idx, key in enumerate(cfg.orders[opt])} for opt in (experiments[0]["plot_rows"] + experiments[0]["plot_cols"])}
    row_combos = sorted(list(row_combos), key=lambda x: [order[opt][dict(x)[opt]] for opt in experiments[0]["plot_rows"]])
    col_combos = sorted(list(col_combos), key=lambda x: [order[opt][dict(x)[opt]] for opt in experiments[0]["plot_cols"]])
    # Prepare data matrix, initialize with minus one
    data_matrix = np.full((len(row_combos), len(col_combos)), float(-11.0))  # -11 means not applicable (initialize with that)
    ci_metrix = np.full((len(row_combos), len(col_combos)), float("nan"))
    fallback_matrix = np.full((len(row_combos), len(col_combos)), 0)
    shortest_matrix = np.full((len(row_combos), len(col_combos)), 0)
    # Create plot
    width = 11                               # Fixed, intended of for single column in paper.
    height = 1 + len(row_combos) * 0.125     # Dynamic based on number of rows
    aspect = ((height - 1) / len(row_combos)) / ((width - 2) / len(col_combos))
    fig, ax = plt.subplots(1,1, figsize=(width,height))
    fig.subplots_adjust(top = 0.93, bottom = 0.3, left = 0.18, right = 0.99)
    for exp in experiments:
        # Compute row and column index
        row_combo = frozenset({opt : exp[opt] for opt in e0["plot_rows"]}.items())
        col_combo = frozenset({opt : exp[opt] for opt in e0["plot_cols"]}.items())
        row_idx = row_combos.index(row_combo)
        col_idx = col_combos.index(col_combo)
        # Missing result
        if exp["name"] not in all_results:
            data_matrix[row_idx, col_idx] = -10.0
            continue
        # Fetch result
        result = all_results[exp["name"]]
        if len(result) == 0:
            data_matrix[row_idx, col_idx] = -10.0           # Missing result
            continue
        # Remove deadlocked runs from result
        result = [r for r in result if not any(r["booksim_details"][key].get("number_of_deadlocked_runs", -1) >= r["booksim_config"]["deadlock_attempts"] for key in r["booksim_details"])]
        all_statuses = [r["summary"]["status"] for r in result]
        majority_status = Counter(all_statuses).most_common(1)[0][0] if len(all_statuses) > 0 else None
        result_suc = [r for r in result if r["summary"]["status"] == "success"]
        # If no non-deadlocked runs remain, mark as deadlocked
        if len(result) == 0:
            data_matrix[row_idx, col_idx] = -12.0           # Deadlock
            continue
        # If there are insufficient VCs. We only check result of rep 0 because either all of them or none have insufficient VCs
        if majority_status == "Insufficient VCs":
            data_matrix[row_idx, col_idx] = -13.0           # Insufficient VCs
            continue
        # If we are plotting the "latency at 0.1 load" metric and the saturation throughput is below 0.1, mark
        result_suf_tp = [r for r in result_suc if r["summary"]["status"] == "success" and "saturation_throughput" in r["summary"] and r["summary"]["saturation_throughput"] >= 0.1]
        if metric == "latency_at_0.1" and len(result_suf_tp) < (len(result) / 2):
            data_matrix[row_idx, col_idx] = -14.0
            continue
        # If this entry in the data matrix is already occupied, print a warning and overwrite later.
        if not data_matrix[row_idx, col_idx] == -11.0:
            hlp.print_yellow("Warning: There seem to be two experiments that map to the same heatmap cell. Overwriting previous value.")
        # Valid result, compute average metric
        value = None
        ci = None
        if metric in ["saturation_throughput", "latency_at_0.1", "zero_load_latency", "energy_per_packet", "average_throughput", "average_latency"]:
            if metric == "latency_at_0.1":
                values = hlp.identify_latencies_at_load(result_suf_tp, 0.1)
                value, lower, upper, ci = hlp.compute_confidence_interval(values, hlp.avg_arithmetic)          # Cost -> Arithmetic mean
            else:
                if metric in ["zero_load_latency", "average_latency"]:
                    values  = [result_suc[i]["summary"][metric] for i in range(len(result_suc))]
                    value, lower, upper, ci = hlp.compute_confidence_interval(values, hlp.avg_arithmetic)       # Cost -> Arithmetic mean
                elif metric in ["saturation_throughput", "average_throughput", "energy_per_packet"]:
                    values = [result_suc[i]["summary"][metric] for i in range(len(result_suc))]
                    value, lower, upper, ci = hlp.compute_confidence_interval(values, hlp.avg_harmonic)         # Rate -> Harmonic mean
        elif metric == "total_power":
            values = [result_suc[i]["summary"]["total_router_power"] + result_suc[i]["summary"]["total_link_power"] for i in range(len(result_suc))]
            calue, lower, upper, ci = hlp.compute_confidence_interval(values, hlp.avg_arithmetic)               # Cost -> Arithmetic mean
        else:
            hlp.print_red("Error: Unknown metric \"%s\" for heatmap plot." % metric)
            sys.exit(1)
        data_matrix[row_idx, col_idx] = value
        ci_metrix[row_idx, col_idx] = ci
        # Also store the additional info if it is present
        add_info_list = [res["stats"]["routing_stats"]["general_stats"][0]["additional_info"] for res in result_suc if len(res["stats"]) > 0 and "additional_info" in res["stats"]["routing_stats"]["general_stats"][0]]
        # Select most common additional info.
        add_info = Counter(add_info_list).most_common(1)[0][0] if len(add_info_list) > 0 else 0
        fallback_matrix[row_idx, col_idx] = add_info
        # Store whether only shortest paths were used
        shortest_list = []
        for res in result_suc:
            if len(res["stats"]) > 0 and "shortest_paths_topology" in res["stats"]["routing_stats"] and "shortest_paths_topology" in res["stats"]["routing_stats"]:
                shortest_topo_hist = {entry["path_length"] : entry["count"] for entry in res["stats"]["routing_stats"]["shortest_paths_topology"]}
                shortest_route_hist = {entry["path_length"] : entry["count"] for entry in res["stats"]["routing_stats"]["shortest_paths_routing"]}
                uses_non_shortest_paths = False
                for (path_length, count) in shortest_topo_hist.items():
                    if path_length not in shortest_route_hist or shortest_route_hist[path_length] != count:
                        uses_non_shortest_paths = True
                        break
                shortest_list.append(2 if uses_non_shortest_paths else 1)
        shortest = Counter(shortest_list).most_common(1)[0][0] if len(shortest_list) > 0 else 0
        shortest_matrix[row_idx, col_idx] = shortest
    # Create heatmap
    special_colors = [[0.8, 0.8, 0.8, 1.0],[0.6, 0.6, 0.6, 1.0],[0.6, 0.6, 0.6, 1.0],[1.0,1.0,1.0,1.0]]  # Deadlock: red, Not applicable: gray, Missing: dark red, While for safety
    gradient = plt.cm.Blues(np.linspace(0, 0.5, 256))
    all_colors = np.vstack([special_colors, gradient])
    cmap = ListedColormap(all_colors)
    min_for_gradient = min([x for x in data_matrix.flatten() if x >= 0.0 and not math.isnan(x)]) if any(x >= 0.0 and not math.isnan(x) for x in data_matrix.flatten()) else 0.0
     # Define bounds for colors
    log_bounds = np.logspace(np.log10(min_for_gradient), np.log10(np.nanmax(data_matrix)), 257)
    bounds = [-12.5, -11.5, -10.5, -9.5] + list(log_bounds)
    norm = BoundaryNorm(bounds, cmap.N)
    im = ax.imshow(data_matrix, cmap=cmap, norm=norm, aspect=aspect)
    # Set ticks and labels
    ax.set_xticks(np.arange(len(col_combos)))
    ax.set_yticks(np.arange(len(row_combos)))
    ax.set_xticklabels(["|".join(f"{cfg.acronyms[opt][dict(col_combo)[opt]]}" for opt in e0["plot_cols"] if cfg.acronyms[opt][dict(col_combo)[opt]] is not None) for col_combo in col_combos], rotation=0, ha="center", fontsize=8, fontname='monospace')
    ax.set_yticklabels(["|".join(f"{cfg.full_names[opt][dict(row_combo)[opt]]}" for opt in e0["plot_rows"] if cfg.full_names[opt][dict(row_combo)[opt]] is not None) for row_combo in row_combos], fontsize=8, fontname='monospace')
    # Loop over data dimensions and create text annotations.
    for i in range(len(row_combos)):
        for j in range(len(col_combos)):
            # Insufficient throughput (for latency at 0.1 load)
            if data_matrix[i, j] == -14.0:
                text = ax.text(j, i, "ITP", ha="center", va="center", color="#000000", fontsize=6, fontweight="normal")
            # Insufficient VCs
            elif data_matrix[i, j] == -13.0:
                text = ax.text(j, i, "IVC", ha="center", va="center", color="#000000", fontsize=6, fontweight="normal")
            # Deadlock
            elif data_matrix[i, j] == -12.0:
                text = ax.text(j, i, "DL", ha="center", va="center", color="#000000", fontsize=6, fontweight="normal")
            # Not applicable
            elif data_matrix[i, j] == -11.0:
                text = ax.text(j, i, "N/A", ha="center", va="center", color="#000000", fontsize=6, fontweight="normal")
            # Missing
            elif data_matrix[i, j] == -10.0:
                text = ax.text(j, i, "MISS", ha="center", va="center", color="#000000", fontsize=6, fontweight="normal")
            # Real value
            elif not math.isnan(data_matrix[i, j]):
                fw = "bold" if round(data_matrix[i, j],3) == round(rank_1_func(data_matrix[:, j]),3) else "normal"
                ul = True if round(data_matrix[i, j],3) == round(rank_2_func(data_matrix[:, j]),3) else False
                #fw = "bold" if data_matrix[i, j] == best_function(data_matrix[:, j]) else "normal"
                val = data_matrix[i, j]
                text_val = str(0 if val == 0 else round(val, 3 - max(0,int(np.floor(np.log10(abs(val)))))))
                if text_val.endswith(".0"):
                    text_val = text_val[:-2]
                # Add CI
                text_val = text_val + ("±%.0f%%" % (100 * ci_metrix[i, j]) if not math.isnan(ci_metrix[i, j]) else "")
                if len(text_val) < 5 and "." in text_val:
                    text_val = text_val + (("0" * (5 - len(text_val))))
                # Append a special character to denote that fallback routing was used
                if fallback_matrix[i, j] == 2:
                    text_val = text_val + r"$^{\dagger}$"
                # Append a special character to denote whether only shortest paths were used
                if shortest_matrix[i, j] == 2:
                    text_val = text_val + r"$^{\ddagger}$"
                text = ax.text(j, i, text_val, ha="center", va="center", color="#000000", fontsize=4.8, fontweight=fw)
                if ul:
                    ax.plot(j, i+0.33, marker="_", color="#000000", markersize=28, markeredgewidth=0.5)
            # NaN value, unclear why this would happen
            else:
                text = ax.text(j, i, "???", ha="center", va="center", color="#000000", fontsize=6, fontweight="normal")

    # Set labels
    ax.set_xlabel(" | ".join([cfg.full_names["parameters"][opt] for opt in e0["plot_cols"]]))
    ax.set_ylabel(" | ".join([cfg.full_names["parameters"][opt] for opt in e0["plot_rows"]]))
    ax.set_title("Heatmap of " + metric.replace("_", " ").title() + r" ($^{\dagger}$Fallback routing used, $^{\ddagger}$Non-shortest paths used)", fontsize=10)
    # Save plot
    if do_save:
        plt.savefig("plots/heatmap_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()
    return (data_matrix, row_combos, col_combos)

def visualize_system(results, adj_list : list[list[int]], latencies : list[list[int]], node_counts : list[int], node_latencies : list[int], router_locations : list[tuple[float,float]], width : float, height : float, name : str, ext_ax = None, title : str = None, exp = None, load : float = None) -> None:
    # Load stats if provided
    link_utilization = {}
    if results is not None and load is not None:
        # Load the link utilization stats from the target file and parse into a dictionary
        stats = results["stats"]
        correct_load_str = None
        for load_str in stats["traffic_stats"]["packets_per_phys_link"].keys():
            if math.isclose(float(load_str), load, rel_tol=1e-9, abs_tol=1e-12):
                correct_load_str = load_str
                break
        if correct_load_str is None:
            hlp.print_error(f"No matching load found in stats file for load {load}")
            sys.exit(1)
        link_utilization = {(x["from_rid"], x["to_rid"]): x["packet_count"] for x in stats["traffic_stats"]["packets_per_phys_link"][correct_load_str]}
    combined_utilization = {}
    for (src, dst), util in link_utilization.items():
        if (dst, src) in combined_utilization:
            combined_utilization[(dst, src)] += util
        else:
            combined_utilization[(src, dst)] = util
    max_utilization = max(combined_utilization.values()) if len(combined_utilization) > 0 else 0.0
    # Create plot
    if ext_ax is None:
        fig, ax = plt.subplots(1,1, figsize=(10,10))
        fig.subplots_adjust(top = 0.95, bottom = 0.05, left = 0.05, right = 0.95)
    else:
        ax = ext_ax
    iv = 0.95 # Inverse spacing between components (spacing is 1/iv)
    n = len(router_locations)
    # Plot components as rectangles and routers as circles
    for (i, (x, y)) in enumerate(router_locations):
        col = "#B8D2F5" if node_counts[i] > 0 else "#CCCCCC66"
        rect = plt.Rectangle((x - iv * width/2, y - iv * height/2), iv * width, iv * height, fill=True, color=col, ec='black', zorder=1)
        ax.add_patch(rect)
    for (i, (x, y)) in enumerate(router_locations):
        circ = plt.Circle((x, y), radius=width/8, fill=True, color='skyblue', ec='black', zorder=3)
        ax.add_patch(circ)
        ax.text(x, y, f"{i}", ha='center', va='center', zorder=4, fontsize=8 / (np.sqrt(n) / 8))
    # Links between routers (as arcs to differentiate links that overlap)
    for src_rid in range(len(adj_list)):
        for dst_rid in adj_list[src_rid]:
            # We use bidirectional links, so only draw one direction
            if src_rid > dst_rid:
                (x1,y1) = router_locations[src_rid]
                (x2,y2) = router_locations[dst_rid]
                # Center of circle
                lf = (np.sqrt((x2 - x1)**2 + (y2 - y1)**2) / math.sqrt((width)**2 + (height)**2))
                af = 100 if lf <= 1 else lf / 2
                xdir, ydir = x2-x1, y2-y1
                ndir = np.sqrt(xdir**2 + ydir**2)
                vx, vy = (-ydir / ndir, xdir / ndir)
                xx, yy = ((x1 + x2) / 2) + af * ndir * vx, ((y1 + y2) / 2) + af * ndir * vy
                # Parameters for the circular arc
                radius = np.sqrt((x1 - xx)**2 + (y1 - yy)**2)  # Radius of the circle
                angle_start = np.arctan2(y1 - yy, x1 - xx)
                angle_end = np.arctan2(y2 - yy, x2 - xx)
                if angle_end < angle_start:
                    angle_end += 2 * np.pi
                # Define the arc
                theta = np.linspace(angle_start, angle_end, 100)
                x_arc = xx + radius * np.cos(theta)
                y_arc = yy + radius * np.sin(theta)
                # Define color based on utilization if available: From Black (low) to Red (high)
                col = "#000000"
                lwd = 1
                util = 0.0
                if (src_rid, dst_rid) in link_utilization:
                    util = link_utilization[(src_rid, dst_rid)]
                if (dst_rid, src_rid) in link_utilization:
                    util += link_utilization[(dst_rid, src_rid)]
                if util > 0.0:
                    norm_util = min((util / max_utilization if max_utilization > 0 else 0.0), 1.0)
                    r = int(255 * norm_util)
                    g = int(0)
                    b = int(0)
                    col = f"#{r:02X}{g:02X}{b:02X}"
                    lwd = 3
                # Plot the arc
                ax.plot(x_arc, y_arc, lw=lwd, color = col, zorder=2)
    # Adjust plot limits
    ax.autoscale_view()
    ax.set_aspect('equal', adjustable='box')
    if title is not None:
        ax.set_title(title)
    # Save plot
    if ext_ax is None:
        plt.savefig("plots/system_visualization_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()

def visualize_distinct_systems(experiments : dict[list[dict]], name : str, load : float = None) -> None:
    distinct_system_names = []
    for experiment in experiments:
        exp = SimpleNamespace(**experiment)
        distinct_system_name = "_".join(str(x) for x in [exp.n_routers, exp.topology, exp.nodes_per_router, exp.width, exp.height])
        if distinct_system_name not in distinct_system_names:
            distinct_system_names.append(distinct_system_name)
            network = gn.generate_network(exp.n_routers, exp.topology, exp.nodes_per_router, exp.link_latency_function, exp.node_link_latency, exp.width, exp.height)
            # Insert faults into the network if configured
            if exp.link_fault_rate > 0.0:
                network = fi.insert_link_faults(network, exp.link_fault_rate, cfg.fault_random_seed, cfg.fault_attempts)
                if network is None:
                    hlp.print_red(f"Failed to insert enough link faults for experiment {exp.name}. Skipping...")
            if exp.router_fault_rate > 0.0:
                network = fi.insert_router_faults(network, exp.router_fault_rate, cfg.fault_random_seed, cfg.fault_attempts)
                if network is None:
                    hlp.print_red(f"Failed to insert enough router faults for experiment {exp.name}. Skipping...")
            nw = SimpleNamespace(**network)
            res = hlp.read_results([experiment])[exp.name][0] if exp.name in hlp.read_results([experiment]) and len(hlp.read_results([experiment])[exp.name]) > 0 else None
            visualize_system(res, nw.adj_list, nw.latencies, nw.node_counts, nw.node_latencies, nw.router_locations, exp.width, exp.height, name + "-" + distinct_system_name, exp = exp, load = load)

def plot_stats_comparison(all_results, name : str) -> None:
    # Load stats
    all_stats = []
    all_names = []
    correct_load_strings = []
    for name, results in all_results.items():
        if "trace" in results[-1]["booksim_details"]:
            correct_load_strings.append("1.000000")
            all_names.append(name)
            all_stats.append(results[-1]["stats"])
        else:
            load = results[-1]["summary"]["saturation_throughput"]
            stats = results[-1]["stats"]
            all_stats.append(stats)
            all_names.append(name)
            for load_str in stats["traffic_stats"]["packets_per_phys_link"].keys():
                if math.isclose(float(load_str), load, rel_tol=1e-9, abs_tol=1e-12):
                    correct_load_strings.append(load_str)
                    break
    # Prepare data
    all_xvalues = []
    all_yvalues = []
    titles = []
    types = []
    for stats, correct_load_str in zip(all_stats, correct_load_strings):
        plt_idx = 0
        for stats_type, stats_data in stats.items():
            for stat_name, data in stats_data.items():
                data = data[correct_load_str] if stats_type == "traffic_stats" else data
                if len(all_xvalues) <= plt_idx:
                    all_xvalues.append([])
                    all_yvalues.append([])
                if "count" in data[0]:
                    xkey = [key for key in data[0].keys() if key != "count"][0]
                    ykey = "count"
                    xvalues = [x[xkey] for x in data]
                    yvalues = [x["count"] for x in data]
                    typ = "bar"
                    # Hack to still plot data with some artifacts from bugs
                    if xvalues[-1] > 1000000000:
                        xvalues = xvalues[:-1]
                        yvalues = yvalues[:-1]
                else:
                    ykeys = [key for key in data[0].keys() if "count" in key]
                    if len(ykeys) == 0:
                        continue
                    ykey = ykeys[0]
                    yvalues = [x[ykey] for x in data]
                    xvalues = list(range(len(yvalues)))
                    yvalues = sorted(yvalues, reverse=True)
                    typ = "line"

                # Normalize he yvalues s.t. they show a percentage of the sum of the yvalues
                if "num_paths" not in stat_name:
                    total = sum(yvalues)
                    if total > 0:
                        yvalues = [y / total * 100 for y in yvalues]

                all_xvalues[plt_idx].append(xvalues)
                all_yvalues[plt_idx].append(yvalues)
                titles.append(stat_name.replace("_", " ").title())
                types.append(typ)
                plt_idx += 1

    # Plot data
    fig, ax = plt.subplots(1, len(all_xvalues), figsize=(len(all_xvalues) * 3, 4))
    fig.subplots_adjust(top = 0.9, bottom = 0.33, left = 0.03, right = 0.99, wspace=0.4)
    for idx in range(len(all_xvalues)):
        subplot_xvalues = all_xvalues[idx]
        subplot_yvalues = all_yvalues[idx]
        max_yvalue = max([max(yvalues) for yvalues in subplot_yvalues if len(yvalues) > 0], default=0.0)
        min_yvalue = min([min(yvalues) for yvalues in subplot_yvalues if len(yvalues) > 0 and min(yvalues) > 0], default=0.0)
        for idx2, (xvalues, yvalues) in enumerate(zip(subplot_xvalues, subplot_yvalues)):
            if types[idx] == "bar":
                offset_xvalues = [x + (idx2 - len(subplot_xvalues)/2) * 0.8 / len(subplot_xvalues) for x in xvalues]
                ax[idx].bar(offset_xvalues, yvalues, color=cfg.colors[idx2 % len(cfg.colors)], zorder=3, label=all_names[idx2], width=0.8 / len(subplot_xvalues))
            elif types[idx] == "line":
                ax[idx].plot(xvalues, yvalues, color=cfg.colors[idx2 % len(cfg.colors)], zorder=3, marker = "none", linewidth=1, label=all_names[idx2])
        ax[idx].set_title(titles[idx], fontsize=10)
        ax[idx].grid(color = "#CCCCCC", linewidth=0.5, zorder=0)
        ax[idx].xaxis.set_major_locator(MaxNLocator(integer=True, min_n_ticks=1))
        ax[idx].set_yscale("log" if max_yvalue > 0 and (min_yvalue == 0 or max_yvalue / min_yvalue > 1000) else "linear")
        ax[idx].set_ylabel("Percentage (%)" if "Num Paths" not in titles[idx] else "Count")
    ax[int(idx/2)].legend(bbox_to_anchor=(0.5, -0.2), loc='upper center', fontsize=10, ncol=len(all_xvalues)/2)
    # Save plot
    plt.savefig("plots/stats_comparison_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()

def plot_stats_histograms(results, name : str, load : float) -> None:
    # Load stats
    stats = results["stats"]
    correct_load_str = None
    for load_str in stats["traffic_stats"]["packets_per_phys_link"].keys():
        if math.isclose(float(load_str), load, rel_tol=1e-9, abs_tol=1e-12):
            correct_load_str = load_str
            break
    if correct_load_str is None:
        hlp.print_error(f"No matching load found in stats file for load {load}")
        sys.exit(1)
    # Prepare datam
    n_routing_stats = sum([sum(1 if "count" in key else 0 for key in data[0].keys()) for data in stats["routing_stats"].values()])
    n_traffic_stats = sum([sum(1 if "count" in key else 0 for key in data[correct_load_str][0].keys()) for data in stats["traffic_stats"].values()])
    n_subplots = n_routing_stats + n_traffic_stats
    # Create plot
    fig, ax = plt.subplots(1, n_subplots, figsize=(n_subplots * 3, 3))
    fig.subplots_adjust(top = 0.85, bottom = 0.25, left = 0.05, right = 0.95, wspace=0.4)
    # Routing stats
    idx = 0
    for stats_type, stats_data in stats.items():
        for stat_name, data in stats_data.items():
            data = data[correct_load_str] if stats_type == "traffic_stats" else data
            if "count" in data[0]:
                xkey = [key for key in data[0].keys() if key != "count"][0]
                ykey = "count"
                xvalues = [x[xkey] for x in data]
                yvalues = [x["count"] for x in data]
                ax[idx].bar(xvalues, yvalues, color=cfg.colors[idx % len(cfg.colors)], zorder=3)
            else:
                xkey = ""
                ykeys = [key for key in data[0].keys() if "count" in key]
                if len(ykeys) == 0:
                    continue
                ykey = ykeys[0]
                yvalues = [x[ykey] for x in data]
                xvalues = list(range(len(yvalues)))
                yvalues = sorted(yvalues, reverse=True)
                ax[idx].plot(xvalues, yvalues, color=cfg.colors[idx % len(cfg.colors)], zorder=3, marker = "none", linewidth=3)
            ax[idx].set_title("Routing: " + stat_name.replace("_", " ").title(), fontsize=10)
            ax[idx].set_xlabel(xkey.replace("_", " ").title() if xkey != "" else "")
            ax[idx].set_ylabel(ykey.replace("_", " ").title())
            ax[idx].grid(color = "#CCCCCC", linewidth=0.5, zorder=0)
            ax[idx].xaxis.set_major_locator(MaxNLocator(integer=True, min_n_ticks=1))
            idx += 1
    # Save plot
    plt.savefig("plots/stats_histograms_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()
    

def plot_latency_vs_load(experiments : list[tuple[str, dict]], name : str) -> None:
    fig, ax = plt.subplots(1,1, figsize=(6, 4))
    fig.subplots_adjust(top = 0.9, bottom = 0.15, left = 0.15, right = 0.95)
    for (idx, (lvl_file, exp)) in enumerate(experiments):
        result = hlp.read_json(lvl_file)
        if len(result) == 0:
            hlp.print_yellow("Warning: No results found for latency-vs-load plot \"%s\". Skipping..." % lvl_file)
            continue
        # TODO: Average over runs
        loads = sorted([float(key) for key in result[0]["booksim_details"].keys() if hlp.is_float(key)])
        latencies = [result[0]["booksim_details"][str(key)]["packet_latency"]["avg"] for key in loads]
        ax.plot(loads, latencies, marker=cfg.markers[idx % len(cfg.markers)], color=cfg.colors[idx % len(cfg.colors)], label=exp["name"])
    ax.set_title("Latency vs Load")
    ax.set_xlabel("Load")
    ax.set_ylabel("Average Latency (cycles)")
    ax.grid(color = "#CCCCCC", linewidth=0.5)
    ax.legend(fontsize=6)
    plt.savefig("plots/latency_vs_load_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)
    plt.close()


def plot_perf_cost_pareto(experiments : list[dict], name : str) -> None:
    # Extract relevant data from experiments
    topology_order = list(cfg.full_names["topology"].keys())
    topologies = sorted(list(set(exp["topology"] for exp in experiments)), key = lambda x: topology_order.index(x) if x in topology_order else len(topology_order))
    routing_functions = sorted(list(set(exp["mod_routing_function"] for exp in experiments)))
    n = len(topologies)
    fig, ax = plt.subplots(2,int(math.ceil(n/2)), figsize=(10, 6))
    fig.subplots_adjust(top = 0.95, bottom = 0.125, left = 0.08, right = 0.99, wspace=0.1, hspace=0.4)
    special_topology_order = ["mesh", "torus", "double_butterfly", "butterdonut", "kite_large","hexamesh", "foldedhexatorus", "folded_torus", "hypercube", "kite_small"]
    for i, topology in enumerate(sorted(topologies, key = lambda x: special_topology_order.index(x) if x in special_topology_order else len(special_topology_order))):
        r = i // int(math.ceil(n/2))
        c = i % int(math.ceil(n/2))
        groups_of_results = {rf : [] for rf in routing_functions}
        topo_experiments = [exp for exp in experiments if exp["topology"] == topology]
        has_results = False
        for j, exp in enumerate(topo_experiments):
            try:
                results = hlp.read_results([exp])[exp["name"]]
            except Exception as e:
                continue
            if len(results) == 0:
                hlp.print_yellow("Warning: No results found for perf-cost pareto plot \"%s\". Skipping..." % exp["name"])
                continue
            if "routing_stats" not in results[-1]["stats"]:
                hlp.print_yellow("Warning: No routing stats found in results for perf-cost pareto plot \"%s\". Skipping..." % exp["name"])
                continue
            groups_of_results[exp["mod_routing_function"]].append(results)
            has_results = True
        # Collect all costs and performances for pareto-optimality check
        all_costs = []
        all_perfs = []
        for group_results in groups_of_results.values():
            for results in group_results:
                if "saturation_throughput" not in results[0]["summary"] or "routing_stats" not in results[0]["stats"]:
                    continue
                perf_values = [round(results[i]["summary"]["saturation_throughput"], 6) for i in range(len(results))]
                perf, _, _, _ = hlp.compute_confidence_interval(perf_values, hlp.avg_harmonic)
                cost = float("inf")
                for rt_storage in ["routing_table_size_csv_bitmap", "routing_table_size_csv_list", "routing_table_size_dense_bitmap", "routing_table_size_dense_list"]:
                    cost_values = [round(results[i]["stats"]["routing_stats"]["general_stats"][0][rt_storage], 6) for i in range(len(results))]
                    cost_tmp, _, _, _ = hlp.compute_confidence_interval(cost_values, hlp.avg_arithmetic)
                    if cost_tmp < cost:
                        cost = cost_tmp
                all_costs.append(cost)
                all_perfs.append(perf)
        for j, (group, group_results) in enumerate(groups_of_results.items()):
            perfs = []
            costs = []
            texts = []
            for results in group_results:
                if "saturation_throughput" not in results[0]["summary"] or "routing_stats" not in results[0]["stats"]:
                    continue
                perf_values = [round(results[i]["summary"]["saturation_throughput"], 6) for i in range(len(results))]
                perf, perf_lower, perf_upper, _ = hlp.compute_confidence_interval(perf_values, hlp.avg_harmonic) # Rate -> Harmonic mean
                cost = float("inf")
                cost_lower = float("inf")
                cost_upper = float("inf")
                for rt_storage in ["routing_table_size_csv_bitmap", "routing_table_size_csv_list", "routing_table_size_dense_bitmap", "routing_table_size_dense_list"]:
                    cost_values = [round(results[i]["stats"]["routing_stats"]["general_stats"][0][rt_storage], 6) for i in range(len(results))]
                    cost_tmp, cost_lower_tmp, cost_upper_tmp, _ = hlp.compute_confidence_interval(cost_values, hlp.avg_arithmetic) # Cost -> Arithmetic mean
                    if cost_tmp < cost:
                        cost = cost_tmp
                        cost_lower = cost_lower_tmp
                        cost_upper = cost_upper_tmp
                nlayer = results[0]["experiment_config"]["n_layers"] if "n_layers" in results[0]["experiment_config"] else None
                fallback = results[0]["stats"]["routing_stats"]["general_stats"][0]["additional_info"] if (len(results[0]["stats"]) > 0 and "additional_info" in results[0]["stats"]["routing_stats"]["general_stats"][0]) else -1
                perfs.append((perf, perf_lower, perf_upper))
                costs.append((cost, cost_lower, cost_upper))
                texts.append(str(nlayer) + (r"$^{\dagger}$" if fallback == 2 else ""))
            lab = str(group)
            mar = cfg.markers[cfg.orders["mod_routing_function"].index(lab) % len(cfg.markers)]
            col = cfg.colors[cfg.orders["mod_routing_function"].index(lab) % len(cfg.colors)]
            col = (*mcolors.to_rgba(col)[:3], 1.0)
            #ax[r][c].plot(costs, perfs, color=col, linewidth=1, zorder=2, marker=mar, markersize=6, markerfacecolor=col, markeredgecolor="none", label=lab)
            x = [cost[0] for cost in costs]
            y = [perf[0] for perf in perfs]
            xerr = [[cost[0]-cost[1] for cost in costs], [cost[2]-cost[0] for cost in costs]]
            yerr = [[perf[0]-perf[1] for perf in perfs], [perf[2]-perf[0] for perf in perfs]]
            ax[r][c].errorbar(x, y, xerr=xerr, yerr=yerr, ecolor=col, elinewidth=1, capsize=2, zorder=5, marker=mar, markersize=6, markerfacecolor=col, markeredgecolor="none", label=lab, color=col, linewidth=1)
            # Highlight pareto-optimal points (those that are not dominated by any other point in terms of both cost and performance)
            is_pareto_optimal = [hlp.is_pareto_optimal(cost[0], perf[0], all_costs, all_perfs) for cost, perf in zip(costs, perfs)]
            for (cost, perf, pareto_optimal) in zip(costs, perfs, is_pareto_optimal):
                if pareto_optimal:
                    n = 10
                    for i in range(n, 0, -1):
                        size = 20 * (i / n) ** 2
                        alpha = 0.6 * ((n-i) / n)
                        ax[r][c].scatter(cost[0], perf[0], s=size**2, c="gold", alpha=alpha, edgecolors="none", zorder=0)

            # Add a small number to display n_layers at each marker in cases with more than one data point per group
            if len(group_results) > 1:
                for k, (cost, perf, text) in enumerate(zip(costs, perfs, texts)):
                    fac = ((3.0/5.0)  if r == 0 else 1.0)
                    pos = max(perf[0] + 0.0125 * fac, perf[2] + 0.01 * fac)
                    ax[r][c].text(cost[0], pos, text, fontsize=6, color=col, zorder=3, ha="center", va="center")
                    #ax[r][c].text(cost, perf + 0.0125, text, fontsize=6, color=col, zorder=3, ha="center", va="center")
        ax[r][c].set_xlabel("Routing Table [bits]")
        ax[r][c].set_yticks([0.0, 0.1, 0.2, 0.3, 0.4, 0.5])
        if c == 0:
            ax[r][c].set_ylabel("Saturation Throughput\n[flits/endpoint/cycle]")
        else:
            ax[r][c].set_yticklabels(["" for _ in ax[r][c].get_yticks()])
        ax[r][c].set_ylim(0,0.23 if r == 0 else 0.46)
        ax[r][c].grid(color = "#CCCCCC", linewidth=0.5)
        ax[r][c].set_title(cfg.full_names["topology"][topology])
        if has_results:
            ax[r][c].set_xscale("log")
        ax[r][c].set_xticks([100,1000,10000])
        ax[r][c].set_xticklabels([r"$10^2$", r"$10^3$", r"$10^4$"], fontsize=7)
    # Legend
    lidx = int(len(topologies)/4)
    handles, labels = ax[1][lidx].get_legend_handles_labels()
    order = sorted(range(len(labels)), key=lambda idx: cfg.orders["mod_routing_function"].index(labels[idx]))
    handles = [handles[idx] for idx in order]
    labels = [cfg.full_names["mod_routing_function"][labels[idx]] for idx in order]
    ax[1][lidx].legend(handles, labels, bbox_to_anchor=(0.5, -0.225), loc='upper center', fontsize=8, ncol=12, handlelength=0.5, handletextpad=0.5, columnspacing=2.5)
    plt.savefig("plots/perf_cost_pareto_%s.%s" % (name, cfg.plot_format), dpi=cfg.plot_dpi)


if __name__ == "__main__":
    # If he option -s or --stats is provided use the following argument to load stats
    # In that case, we just visualize the corresponding system and no not generate all plots
    if "-s" in sys.argv or "--stats" in sys.argv:
        experiment_name = None
        if "-s" in sys.argv:
            idx = sys.argv.index("-s")
            if idx + 1 < len(sys.argv):
                experiment_name = sys.argv[idx + 1]
        elif "--stats" in sys.argv:
            idx = sys.argv.index("--stats")
            if idx + 1 < len(sys.argv):
                experiment_name = sys.argv[idx + 1]
        # Find the right experiment configuration based on the file name
        matched_experiment = None
        for exp_name, experiments in experiment_sweeps.items():
            for exp in experiments:
                if exp["name"] == experiment_name:
                    matched_experiment = exp
                    break
            if matched_experiment is not None:
                break
        if matched_experiment is None:
            hlp.print_red("Error: Could not find matching experiment configuration for experiment name \"%s\"" % experiment_name)
            sys.exit(1)
        else:
            # Read the corresponding results file
            results = hlp.read_results([matched_experiment])[matched_experiment["name"]][-1] # Use last run since we only keep the stats file for the last run
            load = results["summary"]["saturation_throughput"]
            # We create two plots, one that visualizes the system with link utilizations and one with histograms of various stats.
            visualize_distinct_systems([matched_experiment], matched_experiment["name"], load)
            plot_stats_histograms(results, matched_experiment["name"], load)
    # If the option -l or --lvl is provided, we read all files that are given as consecutive arguments and create latency-vs-load plots for them
    elif "-l" in sys.argv:
        lvl_files = []
        idx = sys.argv.index("-l")
        for i in range(idx + 1, len(sys.argv)):
            if sys.argv[i].startswith("-"):
                break
            lvl_files.append(sys.argv[i])
        lvl_experiments = []
        for lvl_file in lvl_files:
            # Find the right experiment configuration based on the file name
            matched_experiment = None
            for exp_name, experiments in experiment_sweeps.items():
                for exp in experiments:
                    if exp["name"] in lvl_file:
                        matched_experiment = exp
                        break
                if matched_experiment is not None:
                    break
            if matched_experiment is None:
                hlp.print_red("Error: Could not find matching experiment configuration for latency-vs-load file \"%s\"" % lvl_file)
                sys.exit(1)
            else:
                lvl_experiments.append((lvl_file, matched_experiment))
        plot_latency_vs_load(lvl_experiments, "latency_vs_load")
    elif "-ce" in sys.argv:
        result_files = []
        idx = sys.argv.index("-ce")
        for i in range(idx + 1, len(sys.argv)):
            if sys.argv[i].startswith("-"):
                break
            result_files.append("results/results_" + sys.argv[i] + ".json")
        all_results = {}
        for result_file in result_files:
            # Find the right experiment configuration based on the file name
            matched_experiment = None
            for exp_name, experiments in experiment_sweeps.items():
                for exp in experiments:
                    if exp["name"] == result_file.replace(".json", "").replace("results/results_", ""):
                        matched_experiment = exp
                        break
                if matched_experiment is not None:
                    break
            if matched_experiment is None:
                hlp.print_red("Error: Could not find matching experiment configuration for stats file \"%s\"" % result_file)
                sys.exit(1)
            else:
                results = hlp.read_json(result_file)
                all_results[matched_experiment["name"]] = results
        plot_stats_comparison(all_results, "stats_comparison")
    elif "-ci" in sys.argv:
        keywords = []
        idx = sys.argv.index("-ci")
        for i in range(idx + 1, len(sys.argv)):
            if sys.argv[i].startswith("-"):
                break
            keywords.append(sys.argv[i])
        all_results = {}
        # Find the right experiment configuration based on the file name
        matched_experiments = []
        for exp_name, experiments in experiment_sweeps.items():
            for exp in experiments:
                match = True
                for keyword in keywords:
                    if keyword not in exp["name"]:
                        match = False
                        break
                if match:
                    matched_experiments.append(exp)
        if len(matched_experiments) == 0:
            hlp.print_red("Error: Could not find matching experiment configuration for keywords \"%s\"" % " ".join(keywords))
            sys.exit(1)
        else:
            for matched_experiment in matched_experiments:
                result_file = "results/results_" + matched_experiment["name"] + ".json"
                all_results[matched_experiment["name"]] = hlp.read_json(result_file)
        plot_stats_comparison(all_results, "stats_comparison")
    elif "-pcp" in sys.argv:
        keywords = []
        idx = sys.argv.index("-pcp")
        for i in range(idx + 1, len(sys.argv)):
            if sys.argv[i].startswith("-"):
                break
            keywords.append(sys.argv[i])
        all_results = {}
        # Find the right experiment configuration based on the file name
        matched_experiments = []
        for exp_name, experiments in experiment_sweeps.items():
            for exp in experiments:
                match = True
                for keyword in keywords:
                    if keyword not in exp["name"]:
                        match = False
                        break
                if match:
                    matched_experiments.append(exp)
        print("Number of matched experiments: %d" % len(matched_experiments))
        # NOTE: This is a bit of a hack, but we also add all experiments from he 64cores sweep to have all baseline routing functions.
        for exp_name, experiments in experiment_sweeps.items():
            if exp_name == "64cores":
                for exp in experiments:
                    if exp["mod_routing_function"] not in ["lash_pat","lash_tor"] and exp["traffic"] == "randperm":
                        mod_exp = copy.deepcopy(exp)
                        mod_exp["vary"] = ["mod_routing_function"]
                        mod_exp["plot_x_axis"] = []
                        matched_experiments.append(mod_exp)
        if len(matched_experiments) == 0:
            hlp.print_red("Error: Could not find matching experiment configuration for keywords \"%s\"" % " ".join(keywords))
            sys.exit(1)
        else:
            for matched_experiment in matched_experiments:
                result_file = "results/results_" + matched_experiment["name"] + ".json"
                try:
                    all_results[matched_experiment["name"]] = hlp.read_json(result_file)
                except Exception as e:
                    hlp.print_yellow(f"Warning: Could not read results file \"{result_file}\" for experiment \"{matched_experiment['name']}\". Skipping this experiment for perf-cost pareto plot. Error: {e}")
        plot_perf_cost_pareto(matched_experiments, "_".join(keywords))
    # No special options, generate all plots as usual
    else:
        # Compose experiments based on configuration
        for exp_name, experiments in experiment_sweeps.items():
            hlp.print_magenta(f"Generating plots for experiment: {exp_name}")
            if experiments[0]["plot_type"] == "lines":
                for metric in ["saturation_throughput", "latency_at_0.1", "zero_load_latency", "energy_per_packet"]:
                    plot_name = exp_name + "_" + metric
                    # Create line plot
                    create_line_plots(experiments, plot_name, metric)
            elif experiments[0]["plot_type"] == "heatmap":
                for metric in ["saturation_throughput", "latency_at_0.1", "zero_load_latency", "energy_per_packet"]:
                    plot_name = exp_name + "_" + metric
                    # Create heatmap plot 
                    heatmap_data = create_heatmap_plot(experiments, plot_name, metric)
                    # Create improvement bar char
                    plot_improvement_bar_chart(experiments, heatmap_data, plot_name, metric, "us_vs_baseline")
            elif experiments[0]["plot_type"] == "heatmap_trace":
                for metric in ["average_throughput", "average_latency", "energy_per_packet"]:
                    plot_name = exp_name + "_" + metric
                    # Create heatmap plot 
                    heatmap_data = create_heatmap_plot(experiments, plot_name, metric)
                    # Create improvement bar char
                    plot_improvement_bar_chart(experiments, heatmap_data, plot_name, metric, "us_vs_baseline")
                    # Create adaptive map size.
            # No visualization needed for now.
            # Visualize distinct systems (do not create multiple plots for same system with different routing or traffic)
            # visualize_distinct_systems(experiments, "all")

