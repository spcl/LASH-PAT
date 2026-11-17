import math


def topology_to_technology(topology):
    if topology in  []:
        return 'noc'
    elif topology in ["mesh","folded_torus","hexamesh","foldedhexatorus"]:
        return 'pici'
    elif topology in ["torus","hypercube","butterdonut","kite_small","kite_large","double_butterfly"]:
        return 'aici'
    elif topology in ["wsi_aligned","wsi_interleaved"]:
        return 'wsi'
    else:
        print("Error in insert_technology_dependent_parameters: topology \"" + topology + "\" not recognized")

def insert_technology_dependent_parameters(exp, topology):
    tech = topology_to_technology(topology)
    if tech == 'noc':
        exp['link_latency_function'] = lambda length : int(math.ceil(0.5 * length))
        exp['node_link_latency'] = 5
        exp['width'] = 3.25
        exp['height'] = 3.25
        exp['data_width'] = 64
    elif tech == 'pici':
        exp['link_latency_function'] = lambda length : int(math.ceil(4 + 0.5 * length))
        exp['node_link_latency'] = 5
        exp['width'] = 8.6
        exp['height'] = 8.6
        exp['data_width'] = 64
    elif tech == 'aici':
        exp['link_latency_function'] = lambda length : int(math.ceil(4 + 0.5 * length))
        exp['node_link_latency'] = 5
        exp['width'] = 8.6
        exp['height'] = 8.6
        exp['data_width'] = 64
    elif tech == 'wsi':
        exp['link_latency_function'] = lambda length : int(math.ceil(0.5 * length))
        exp['node_link_latency'] = 5
        exp['width'] = 26
        exp['height'] = 33
        exp['data_width'] = 16000
    else:
        print("Error in insert_technology_dependent_parameters: technology \"" + tech + "\" not recognized")


def get_lash_pat_n_additional_paths(topology):
    return 3
    """
    if topology in  ["kite_small"]:
        return 2
    elif topology in ["mesh","folded_torus","foldedhexatorus","double_butterfly","butterdonut","kite_large"]:
        return 3
    elif topology in ["hexamesh"]:
        return 4
    elif topology in ["torus","hypercube"]:
        return 5
    else:
        print("Error in get_lash_pat_n_additional_paths: topology \"" + topology + "\" not recognized")
    """
