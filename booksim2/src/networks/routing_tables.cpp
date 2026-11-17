#include "routing_tables.hpp"
#include <fstream>
#include <cereal/archives/binary.hpp>

// Routing Table Type 1: (cur_rid, dst_rid, in_port, in_vc) -> [(out_port, out_vc, path_length, nxt_rid),...]
void RoutingTableType1::save(const std::string &filename) const {
    std::vector<RoutingEntryType1> flat;
    for (const auto &[cur_rid, m_dst] : _routing_table)
        for (const auto &[dst_rid, m_pair] : m_dst)
            for (const auto &[in, vec] : m_pair)
                flat.push_back({cur_rid, dst_rid, in.first, in.second, vec});

    std::ofstream os(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    archive(flat, _additional_info);
}

void RoutingTableType1::load(const std::string &filename) const {
    std::vector<RoutingEntryType1> flat;

    std::ifstream is(filename, std::ios::binary);
    if (!is)
        return;

    cereal::BinaryInputArchive archive(is);
    archive(flat, _additional_info);

    _routing_table.clear();

    for (const auto &e : flat)
        _routing_table[e.cur_rid][e.dst_rid][{e.in_port, e.in_vc}] = e.values;
}

// Routing Table Type 2: (cur_rid, dst_rid, in_port) -> [out_port, ...]
void RoutingTableType2::save(const std::string &filename) const {
    std::vector<RoutingEntryType2> flat;
    for (const auto &[cur_rid, m_dst] : _routing_table)
        for (const auto &[dst_rid, m_pair] : m_dst)
            for (const auto &[in_port, vec] : m_pair)
                flat.push_back({cur_rid, dst_rid, in_port, vec});

    std::ofstream os(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    archive(flat);
}

void RoutingTableType2::load(const std::string &filename) const {
    std::vector<RoutingEntryType2> flat;

    std::ifstream is(filename, std::ios::binary);
    if (!is)
        return;

    cereal::BinaryInputArchive archive(is);
    archive(flat);

    _routing_table.clear();

    for (const auto &e : flat)
        _routing_table[e.cur_rid][e.dst_rid][e.in_port] = e.values;
}

// Routing Table Type 3: (cur_rid, dst_rid) -> out_port
void RoutingTableType3::save(const std::string &filename) const {
    std::vector<RoutingEntryType3> flat;
    for (const auto &[cur_rid, m_dst] : _routing_table)
        for (const auto &[dst_rid, val] : m_dst)
        	flat.push_back({cur_rid, dst_rid, val});

    std::ofstream os(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    archive(flat);
}

void RoutingTableType3::load(const std::string &filename) const {
    std::vector<RoutingEntryType3> flat;

    std::ifstream is(filename, std::ios::binary);
    if (!is)
        return;

    cereal::BinaryInputArchive archive(is);
    archive(flat);

    _routing_table.clear();

    for (const auto &e : flat)
        _routing_table[e.cur_rid][e.dst_rid] = e.value;
}

// Routing Table Type 4: (cur_rid, dst_rid) -> out_port
void RoutingTableType4::save(const std::string &filename) const {
    std::vector<RoutingEntryType4> flat;
    for (const auto &[cur_rid, m_dst] : _routing_table)
		for (const auto &[dst_rid, val] : m_dst)
			flat.push_back({cur_rid, dst_rid, val});

    std::vector<LayerEntryType4> flat_layer;
    for (const auto &[src_rid, m_dst] : _layer_map)
        for (const auto &[dst_rid, layer] : m_dst)
            flat_layer.push_back({src_rid, dst_rid, layer});

    std::vector<VCEntryType4> flat_vc;
    for (const auto &[layer, vcs] : _vc_map)
        flat_vc.push_back({layer, vcs});

    std::ofstream os(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    archive(flat, flat_layer, flat_vc);
}

void RoutingTableType4::load(const std::string &filename) const {
    std::vector<RoutingEntryType4> flat;
    std::vector<LayerEntryType4> flat_layer;
    std::vector<VCEntryType4> flat_vc;

    std::ifstream is(filename, std::ios::binary);
    if (!is)
        return;

    cereal::BinaryInputArchive archive(is);
    archive(flat, flat_layer, flat_vc);

    _routing_table.clear();
    _layer_map.clear();
    _vc_map.clear();

    for (const auto &e : flat)
        _routing_table[e.cur_rid][e.dst_rid] = e.value;

    for (const auto &e : flat_layer)
        _layer_map[e.src_rid][e.dst_rid] = e.layer;

    for (const auto &e : flat_vc)
        _vc_map[e.layer] = e.vcs;
}

// Routing Table Type 5: (cur_rid, dst_rid, in_port, in_vc) -> (out_port, out_vc, nxt_rid)
void RoutingTableType5::save(const std::string &filename) const {
    std::vector<RoutingEntryType5> flat;
    for (const auto &[cur_rid, m_dst] : _routing_table)
        for (const auto &[dst_rid, m_pair] : m_dst)
            for (const auto &[in, tup] : m_pair)
                flat.push_back({cur_rid, dst_rid, in.first, in.second, tup});

    std::ofstream os(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    archive(flat, _additional_info);
}

void RoutingTableType5::load(const std::string &filename) const {
    std::vector<RoutingEntryType5> flat;

    std::ifstream is(filename, std::ios::binary);
    if (!is)
        return;

    cereal::BinaryInputArchive archive(is);
    archive(flat, _additional_info);

    _routing_table.clear();

    for (const auto &e : flat)
        _routing_table[e.cur_rid][e.dst_rid][{e.in_port, e.in_vc}] = e.value;
}



// Routing Table Type 6: (cur_rid, dst_rid) -> out_port
void RoutingTableType6::save(const std::string &filename) const {
    std::vector<RoutingEntryType6> flat;
    for (const auto &[cur_rid, m_dst] : _routing_table)
		for (const auto &[dst_rid, vals] : m_dst)
			flat.push_back({cur_rid, dst_rid, vals});

    std::vector<LayerEntryType6> flat_layer;
    for (const auto &[cur_rid, m_dst] : _layer_map)
		for (const auto &[dst_rid, m_port] : m_dst)
			for (const auto &[out_port, layers] : m_port)
				flat_layer.push_back({cur_rid, dst_rid, out_port, layers});

    std::ofstream os(filename, std::ios::binary);
    cereal::BinaryOutputArchive archive(os);
    archive(flat, flat_layer);
}

void RoutingTableType6::load(const std::string &filename) const {
    std::vector<RoutingEntryType6> flat;
	std::vector<LayerEntryType6> flat_layer;

    std::ifstream is(filename, std::ios::binary);
    if (!is)
        return;

    cereal::BinaryInputArchive archive(is);
    archive(flat, flat_layer);

    _routing_table.clear();
	_layer_map.clear();

    for (const auto &e : flat)
        _routing_table[e.cur_rid][e.dst_rid] = e.values;

	for (const auto &e : flat_layer)
		_layer_map[e.src_rid][e.dst_rid][e.out_port] = e.layers;
}
