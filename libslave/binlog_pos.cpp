#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>

#include <alloca.h>
#include <mysql/my_global.h>
#undef min
#undef max
#undef test

#include "binlog_pos.h"
#include "slave_log_event.h"

namespace
{
void hex2bin(uint8_t* dst, const char* src, size_t sz_src)
{
    if (!dst || !src) return;

    uint8_t cur = 0;
    for (size_t i = 0; i < sz_src; ++i)
    {
        const char c = src[i];

             if ('0' <= c && c <= '9') cur |= c - '0';
        else if ('A' <= c && c <= 'F') cur |= c - 'A' + 10;
        else if ('a' <= c && c <= 'f') cur |= c - 'a' + 10;
        else throw std::runtime_error("hex2bin failed: bad symbol in hex data");

        if (0 == i % 2)
        {
            cur <<= 4;
        }
        else
        {
            *dst++ = cur;
            cur = 0;
        }
    }
}

template <class F>
void backup_invoke_restore(F& f, char* begin, char* end)
{
    if (begin == end)
        return;
    const char backup = *end;
    *end = '\0';
    f(const_cast<const char*>(begin));
    *end = backup;
}

// Parse list on const char* using delimiter and call function with each element
template <class F, class C>
void parse_list_f_custom(const std::string& aList, F f, C c, const char* delim = ",")
{
    std::unique_ptr<char[]> sHeap;
    char* s = nullptr;
    const size_t sRequiredSize = aList.size() + 1;
    if (sRequiredSize <= 65536)
    {
        s = (char*)::alloca(sRequiredSize);
    }
    else
    {
        sHeap.reset(new char[sRequiredSize]);
        s = sHeap.get();
    }

    memcpy(s, aList.data(), aList.size());
    s[aList.size()] = '\0';

    for (char* p = s; '\0' != *p;)
    {
        p += strspn(p, delim);
        if (!c(aList, f, p))
        {
            auto q = p + strcspn(p, delim);
            backup_invoke_restore(f, p, q);
            p = q;
        }
    }
}

template <typename F>
void parse_list_f(const std::string& aList, F f, const char* delim = ",")
{
    parse_list_f_custom(aList, f, [] (const std::string&, F&, char*) { return false; }, delim);
}

// Parse list on long long using delimiter and call function with each element
template <typename F>
void parse_list_ll_f(const std::string& aList, F f, const char* delim = ",")
{
    parse_list_f(aList, [&f] (const char* s) { f(atoll(s)); }, delim);
}

// Parse list using delimiter and put strings into container
template <class Cont>
void parse_list_cont(const std::string& aList, Cont& aCont, const char* delim = ",")
{
    parse_list_f(aList,
            [&aCont] (const char* s)
            {
                aCont.insert(aCont.end(), s);
            }
        , delim);
}
} // namespace anonymous

namespace slave
{

// parseGtid parse string with gtid
// example:  ae00751a-cb5f-11e6-9d92-e03f490fd3db:1-12:15-17
// gtid_set: uuid_set [, uuid_set] ... | ''
// uuid_set: uuid:interval[:interval]...
// uuid:     hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh
// h:        [0-9|A-F]
// interval: n[-n] (n >= 1)
void Position::parseGtid(const std::string& input)
{
    if (input.empty())
        return;
    gtid_executed.clear();
    std::string s;
    std::remove_copy_if(input.begin(), input.end(), std::back_inserter(s), [](char c){ return c == ' ' || c == '\n'; });

    std::deque<std::string> cont;
    parse_list_f(s, [this, &cont](const std::string& token)
    {
        cont.clear();
        parse_list_cont(token, cont, ":");
        bool uuid_parsed = false;
        std::string sid;
        for (const auto& x : cont)
        {
            if (!uuid_parsed)
            {
                std::remove_copy(x.begin(), x.end(), std::back_inserter(sid), '-');
                uuid_parsed = true;
            }
            else
            {
                bool first = true;
                gtid_interval_t interval;
                parse_list_ll_f(x, [&first, &interval](int64_t y)
                {
                    if (first)
                    {
                        interval.first = interval.second = y;
                        first = false;
                    }
                    else
                    {
                        interval.second = y;
                    }
                }, "-");
                gtid_executed[sid].push_back(interval);
            }
        }
    });
}

void Position::addGtid(const gtid_t& gtid)
{
    auto it = gtid_executed.find(gtid.first);
    if (it == gtid_executed.end())
    {
        gtid_executed[gtid.first].emplace_back(gtid.second, gtid.second);
        return;
    }

    bool flag = true;
    for (auto itr = it->second.begin(); itr != it->second.end(); ++itr)
    {
        if (gtid.second >= itr->first && gtid.second <= itr->second)
            return;
        if (gtid.second + 1 == itr->first)
        {
            --itr->first;
            flag = false;
            break;
        }
        else if (gtid.second == itr->second + 1)
        {
            ++itr->second;
            flag = false;
            break;
        }
        else if (gtid.second < itr->first)
        {
            it->second.emplace(itr, gtid.second, gtid.second);
            return;
        }
    }

    if (flag)
        it->second.emplace_back(gtid.second, gtid.second);

    for (auto itr = it->second.begin(); itr != it->second.end(); ++itr)
    {
        auto next_itr = std::next(itr);
        if (next_itr != it->second.end() && itr->second + 1 == next_itr->first)
        {
            itr->second = next_itr->second;
            it->second.erase(next_itr);
            break;
        }
    }
}

size_t Position::encodedGtidSize() const
{
    if (gtid_executed.empty())
        return 0;
    size_t result = 8;
    for (const auto& x : gtid_executed)
        result += x.second.size() * 16 + 8 + ENCODED_SID_LENGTH;

    return result;
}

void Position::encodeGtid(unsigned char* buf)
{
    if (gtid_executed.empty())
        return;
    int8store(buf, gtid_executed.size());
    size_t offset = 8;
    for (const auto& x : gtid_executed)
    {
        hex2bin(buf + offset, x.first.c_str(), ENCODED_SID_LENGTH * 2);
        offset += ENCODED_SID_LENGTH;
        int8store(buf + offset, x.second.size());
        offset += 8;
        for (const auto& interval : x.second)
        {
            int8store(buf + offset, interval.first);
            offset += 8;
            int8store(buf + offset, interval.second + 1);
            offset += 8;
        }
    }
}

bool Position::reachedOtherPos(const Position& other) const
{
    if (gtid_executed.empty())
        return log_name > other.log_name ||
               (log_name == other.log_name && log_pos >= other.log_pos);
    return gtid_executed == other.gtid_executed;
}

std::string Position::str() const
{
    std::string result = "'";
    if (!log_name.empty() && log_pos)
        result += log_name + ":" + std::to_string(log_pos) + ", ";

    result += "GTIDs=";
    if (gtid_executed.empty())
    {
        result += "-'";
        return result;
    }

    bool first_a = true;
    for (const auto& gtid : gtid_executed)
    {
        if (first_a)
            first_a = false;
        else
            result += ",";

        result += gtid.first + ":";
        bool first_b = true;
        for (const auto& interv : gtid.second)
        {
            if (first_b)
                first_b = false;
            else
                result += ":";

            result += std::to_string(interv.first);
            if (interv.first != interv.second)
                result += "-" + std::to_string(interv.second);
        }
    }
    result += "'";
    return result;
}

} // namespace slave
