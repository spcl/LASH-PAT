#pragma once

#include <map>
#include <vector>
#include <tuple>
#include <utility>
#include <string>

// cereal includes needed for templates
#include <cereal/types/vector.hpp>
#include <cereal/types/tuple.hpp>


// Type 1 routing table semantics: routing_table[cur_rid][dst_rid][(in_port, in_vc)] = [ (out_port_1, out_vc_1, path_length_1, next_rid_1), ... ]
struct RoutingEntryType1 {
    int cur_rid;
    int dst_rid;
    int in_port;
    int in_vc;
    std::vector<std::tuple<int,int,int,int>> values;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cur_rid, dst_rid, in_port, in_vc, values);
    }
};

class RoutingTableType1 {
public:
    using Table = std::map<int, std::map< int, std::map< std::pair<int,int>, std::vector<std::tuple<int,int,int,int>> > > >;
    mutable Table _routing_table;
	mutable int _additional_info;

    void save(const std::string &filename) const;
    void load(const std::string &filename) const;
};

// Type 2 routing table semantics: routing_table[cur_rid][dst_rid][in_port] = [out_port_1, ...]
struct RoutingEntryType2 {
    int cur_rid;
    int dst_rid;
    int in_port;
    std::vector<int> values;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cur_rid, dst_rid, in_port, values);
    }
};

class RoutingTableType2 {
public:
    using Table = std::map<int, std::map< int, std::map<int, std::vector<int> > > >;
    mutable Table _routing_table;
    void save(const std::string &filename) const;
    void load(const std::string &filename) const;
};

// Type 3 routing table semantics: routing_table[cur_rid][dst_rid] = [out_port_1, ...]
struct RoutingEntryType3 {
    int cur_rid;
    int dst_rid;
    int value;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cur_rid, dst_rid, value);
    }
};

class RoutingTableType3 {
public:
    using Table = std::map<int, std::map< int, int > >;
    mutable Table _routing_table;
    void save(const std::string &filename) const;
    void load(const std::string &filename) const;
};

// Type 4 routing table semantics: routing_table[cur_rid][dst_rid] = [out_port_1, ...]
struct RoutingEntryType4 {
    int cur_rid;
    int dst_rid;
    int value;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cur_rid, dst_rid, value);
    }
};

struct LayerEntryType4 {
    int src_rid;
    int dst_rid;
    int layer;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(src_rid, dst_rid, layer);
    }
};

struct VCEntryType4 {
    int layer;
    std::vector<int> vcs;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(layer, vcs);
    }
};

class RoutingTableType4 {
public:
    using Table = std::map<int, std::map<int,int>>;

    mutable Table _routing_table;
    mutable std::map<int,std::map<int,int>> _layer_map;
    mutable std::map<int,std::vector<int>> _vc_map;

    void save(const std::string &filename) const;
    void load(const std::string &filename) const;
};


// Type 5 routing table semantics: routing_table[cur_rid][dst_rid][(in_port, in_vc)] = (out_port_5, out_vc_5, next_rid_5)
struct RoutingEntryType5 {
    int cur_rid;
    int dst_rid;
    int in_port;
    int in_vc;
    std::tuple<int,int,int> value;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cur_rid, dst_rid, in_port, in_vc, value);
    }
};

class RoutingTableType5 {
public:
    using Table = std::map<int, std::map< int, std::map< std::pair<int,int>, std::tuple<int,int,int> > > >;
    mutable Table _routing_table;
	mutable int _additional_info;

    void save(const std::string &filename) const;
    void load(const std::string &filename) const;
};



// Type 6 routing table semantics: routing_table[cur_rid][dst_addr] = [out_port_1, ...]
struct RoutingEntryType6 {
    int cur_rid;
    int dst_rid;
	std::vector<int> values;

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cur_rid, dst_rid, values);
    }
};

struct LayerEntryType6 {
	int src_rid;
    int dst_rid;
    int out_port;
	std::vector<int> layers;

    template<class Archive>
    void serialize(Archive &ar) {
		ar(src_rid, dst_rid, out_port, layers);
    }
};

class RoutingTableType6 {
public:
    using Table = std::map<int, std::map<int,std::vector<int>>>;

    mutable Table _routing_table;
    mutable std::map<int,std::map<int,std::map<int,std::vector<int>>>> _layer_map;

    void save(const std::string &filename) const;
    void load(const std::string &filename) const;
};

