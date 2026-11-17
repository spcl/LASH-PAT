#include "statistics.hpp"
#include <fstream>
#include <cctype>

// ---------------- utils ----------------
void Statistics::skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
}

std::string Statistics::escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

std::string Statistics::unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i++;
            out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

// ---------------- parsing primitives ----------------
std::string Statistics::parseString(const std::string& s, size_t& i) {
    skipWs(s, i);
    if (s[i] != '"') return "";
    i++;
    std::string out;

    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i++;
            out += s[i];
        } else {
            out += s[i];
        }
        i++;
    }
    i++;
    return out;
}

std::string Statistics::parseKey(const std::string& s, size_t& i) {
    return parseString(s, i);
}

int Statistics::parseInt(const std::string& s, size_t& i) {
    skipWs(s, i);
    int sign = 1;
    if (s[i] == '-') { sign = -1; i++; }

    int val = 0;
    while (i < s.size() && std::isdigit(s[i])) {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    return val * sign;
}

float Statistics::parseFloat(const std::string& s, size_t& i) {
    skipWs(s, i);
    return std::stof(parseString(s, i));
}

// ---------------- routing stats ----------------
Statistics::RoutingStats Statistics::parseRoutingStats(const std::string& s, size_t& i) {
    RoutingStats rs;

    skipWs(s, i);
    i++; // {

    while (true) {
        skipWs(s, i);
        if (s[i] == '}') { i++; break; }

        std::string key = parseKey(s, i);

        skipWs(s, i);
        i++; // :

        skipWs(s, i);
        i++; // [

        std::vector<RoutingEntry> vec;

        while (true) {
            skipWs(s, i);
            if (s[i] == ']') { i++; break; }

            i++; // {
            RoutingEntry entry;

            while (true) {
                skipWs(s, i);
                if (s[i] == '}') { i++; break; }

                std::string k = parseKey(s, i);
                skipWs(s, i);
                i++; // :
                int v = parseInt(s, i);

                entry[k] = v;

                skipWs(s, i);
                if (s[i] == ',') i++;
            }

            vec.push_back(entry);

            skipWs(s, i);
            if (s[i] == ',') i++;
        }

        rs[key] = vec;

        skipWs(s, i);
        if (s[i] == ',') i++;
    }

    return rs;
}

// ---------------- traffic stats ----------------
Statistics::TrafficStats Statistics::parseTrafficStats(const std::string& s, size_t& i) {
    TrafficStats ts;

    skipWs(s, i);
    i++; // {

    while (true) {
        skipWs(s, i);
        if (s[i] == '}') { i++; break; }

        std::string outerKey = parseKey(s, i);

        skipWs(s, i);
        i++; // :

        skipWs(s, i);
        i++; // {

        std::map<float, std::vector<TrafficEntry>> inner;

        while (true) {
            skipWs(s, i);
            if (s[i] == '}') { i++; break; }

            float fkey = parseFloat(s, i);

            skipWs(s, i);
            i++; // :

            skipWs(s, i);
            i++; // [

            std::vector<TrafficEntry> vec;

            while (true) {
                skipWs(s, i);
                if (s[i] == ']') { i++; break; }

                i++; // {
                TrafficEntry entry;

                while (true) {
                    skipWs(s, i);
                    if (s[i] == '}') { i++; break; }

                    std::string k = parseKey(s, i);
                    skipWs(s, i);
                    i++; // :
                    int v = parseInt(s, i);

                    entry[k] = v;

                    skipWs(s, i);
                    if (s[i] == ',') i++;
                }

                vec.push_back(entry);

                skipWs(s, i);
                if (s[i] == ',') i++;
            }

            inner[fkey] = vec;

            skipWs(s, i);
            if (s[i] == ',') i++;
        }

        ts[outerKey] = inner;

        skipWs(s, i);
        if (s[i] == ',') i++;
    }

    return ts;
}

// ---------------- writing ----------------
void Statistics::writeRoutingStats(std::string& out, const RoutingStats& rs) {
    out += "\"routing_stats\":{\n";
    bool firstKey = true;

    for (const auto& [k, vec] : rs) {
        if (!firstKey) out += ",\n";
        firstKey = false;

        out += "\t\"" + escape(k) + "\":[\n";
        bool firstVec = true;

        for (const auto& entry : vec) {
            if (!firstVec) out += ",\n";
            firstVec = false;

            out += "\t\t{\n";
            bool first = true;

            for (const auto& [ek, ev] : entry) {
                if (!first) out += ",\n";
                first = false;

                out += "\t\t\t\"" + escape(ek) + "\":" + std::to_string(ev);
            }
            out += "\n\t\t}";
        }

        out += "\n\t]";
    }

    out += "\n}";
}

void Statistics::writeTrafficStats(std::string& out, const TrafficStats& ts) {
    out += "\"traffic_stats\":{\n";
    bool firstOuter = true;

    for (const auto& [ok, inner] : ts) {
        if (!firstOuter) out += ",\n";
        firstOuter = false;

        out += "\t\"" + escape(ok) + "\":{\n";
        bool firstInner = true;

        for (const auto& [fk, vec] : inner) {
            if (!firstInner) out += ",\n";
            firstInner = false;

            out += "\t\t\"" + std::to_string(fk) + "\":[\n";
            bool firstVec = true;

            for (const auto& entry : vec) {
                if (!firstVec) out += ",\n";
                firstVec = false;

                out += "\t\t\t{\n";
                bool first = true;

                for (const auto& [ek, ev] : entry) {
                    if (!first) out += ",\n";
                    first = false;

                    out += "\t\t\t\t\"" + escape(ek) + "\":" + std::to_string(ev);
                }

                out += "\n\t\t\t}";
            }

            out += "\n\t\t]";
        }

        out += "\n\t}";
    }

    out += "\n}";
}

// ---------------- public API ----------------
bool Statistics::save(const std::string& filename) const {
    std::string out = "{\n";

    writeRoutingStats(out, _routing_stats);
    out += ",\n";
    writeTrafficStats(out, _traffic_stats);

    out += "}";

    std::ofstream f(filename);
    if (!f.is_open()) return false;

    f << out;
    return true;
}

bool Statistics::load(const std::string& filename) {
    std::ifstream f(filename);
    if (!f.is_open()) return false;

    std::string s((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());

    size_t i = 0;
    skipWs(s, i);
    if (s[i] != '{') return false;
    i++;

    while (i < s.size()) {
        skipWs(s, i);
        if (s[i] == '}') break;

        std::string key = parseKey(s, i);
        skipWs(s, i);
        i++; // :

        if (key == "routing_stats") {
            _routing_stats = parseRoutingStats(s, i);
        } else if (key == "traffic_stats") {
            _traffic_stats = parseTrafficStats(s, i);
        }

        skipWs(s, i);
        if (s[i] == ',') i++;
    }

    return true;
}
