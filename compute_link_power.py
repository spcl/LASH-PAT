# Python modules
import os
import sys
import math

# Custom modules
import helpers as hlp
import config as cfg


def compute_link_power(adj_list : list[list[int]], lengths: list[list[float]], run_identifier : str, load : float, freq : int, cycles : int, packet_size : int, data_width : int, stats):
    correct_load_str = None
    for load_str in stats["traffic_stats"]["packets_per_phys_link"].keys():
        if math.isclose(float(load_str), load, rel_tol=1e-9, abs_tol=1e-12):
            correct_load_str = load_str
            break
    if correct_load_str is None:
        hlp.print_error(f"No matching load found in stats file for load {load}")
        sys.exit(1)
    link_utilization = {(x["from_rid"], x["to_rid"]): x["packet_count"] for x in stats["traffic_stats"]["packets_per_phys_link"][correct_load_str]}
    # Precomputed factors 
    bits_per_packet = packet_size * data_width                                          # bits
    energy_per_packet_per_mm = bits_per_packet * cfg.energy_per_bit_per_mm_7nm_in_pj    # pJ/mm
    # Compute total energy of all links in the network
    total_energy = 0.0
    for src_rid in range(len(adj_list)):
        for (i, dst_rid) in enumerate(adj_list[src_rid]):
            # Check if this link was used
            if (src_rid, dst_rid) in link_utilization:
                packet_count = link_utilization[(src_rid, dst_rid)]
                link_length = lengths[src_rid][i]
                total_energy += packet_count * energy_per_packet_per_mm * link_length
    # Compute energy per second (power in Watts)
    total_time_seconds = cycles / freq
    power_watts = total_energy / total_time_seconds * 1e-12                             # Convert pJ to W
    # Return the result as a list with a single dictionary
    return power_watts

