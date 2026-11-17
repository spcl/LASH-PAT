#ifndef STATISTICS_HPP
#define STATISTICS_HPP

#include <string>
#include <map>
#include <vector>

class Statistics {
public:
    using RoutingEntry = std::map<std::string, int>;
    using RoutingStats = std::map<std::string, std::vector<RoutingEntry>>;

    using TrafficEntry = std::map<std::string, int>;
    using TrafficStats = std::map<std::string, std::map<float, std::vector<TrafficEntry>>>;

    bool load(const std::string& filename);
    bool save(const std::string& filename) const;

    RoutingStats& routingStats() { return _routing_stats; }
    TrafficStats& trafficStats() { return _traffic_stats; }

private:
    RoutingStats _routing_stats;
    TrafficStats _traffic_stats;

    static std::string escape(const std::string& s);
    static std::string unescape(const std::string& s);

    static void skipWs(const std::string& s, size_t& i);

    static std::string parseString(const std::string& s, size_t& i);
    static int parseInt(const std::string& s, size_t& i);
    static float parseFloat(const std::string& s, size_t& i);

    static std::string parseKey(const std::string& s, size_t& i);

    static RoutingStats parseRoutingStats(const std::string& s, size_t& i);
    static TrafficStats parseTrafficStats(const std::string& s, size_t& i);

    static void writeRoutingStats(std::string& out, const RoutingStats& rs);
    static void writeTrafficStats(std::string& out, const TrafficStats& ts);
};

#endif
