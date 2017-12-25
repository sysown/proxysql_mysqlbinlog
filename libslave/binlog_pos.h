#pragma once

#include <list>
#include <string>
#include <ostream>
#include <unordered_map>
#include <utility>

namespace slave
{

// interval of transactions with numbers from "first" to "second"
using gtid_interval_t = std::pair<int64_t, int64_t>;
// set of transactions:
// key - source server uuid, value - list of transaction intervals
using gtid_set_t = std::unordered_map<std::string, std::list<gtid_interval_t>>;
// single transaction: first - server uuid, second - transaction number
using gtid_t = std::pair<std::string, int64_t>;

struct Position
{
    std::string   log_name;
    unsigned long log_pos = 0;
    gtid_set_t    gtid_executed;

    Position() {}

    Position(std::string _log_name, unsigned long _log_pos)
    :   log_name(std::move(_log_name))
    ,   log_pos(_log_pos)
    {}

    bool empty() const { return (log_name.empty() || log_pos == 0) && gtid_executed.empty(); }
    void clear() { log_name.clear(); log_pos = 0; gtid_executed.clear(); }

    void parseGtid(const std::string& input);
    void addGtid(const gtid_t& gtid);
    size_t encodedGtidSize() const;
    void encodeGtid(unsigned char* buf);

    bool reachedOtherPos(const Position& other) const;

    std::string str() const;
};

inline std::ostream& operator<<(std::ostream& os, const Position& pos)
{
    os << pos.str();
    return os;
}

} // namespace slave
