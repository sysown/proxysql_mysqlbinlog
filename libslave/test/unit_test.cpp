#include <boost/test/included/unit_test.hpp>
using namespace boost::unit_test;

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/mpl/int.hpp>
#include <boost/mpl/list.hpp>
#include <boost/optional.hpp>

#include <cfloat>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include "Slave.h"
#include "nanomysql.h"
#include "types.h"

namespace // anonymous
{
    const std::string TestDataDir = "test/data/";

    struct config
    {
        std::string mysql_host;
        int         mysql_port;
        std::string mysql_db;
        std::string mysql_user;
        std::string mysql_pass;

        config()
        :   mysql_host("localhost")
        ,   mysql_port(3306)
        ,   mysql_db("test")
        ,   mysql_user("root")
        {}

        void load(const std::string& fn)
        {
            std::ifstream f(fn.c_str());
            if (!f)
                throw std::runtime_error("can't open config file '" + fn + "'");

            std::string line;
            while (getline(f, line))
            {
                if (line.empty())
                    continue;
                std::vector<std::string> tokens;
                boost::algorithm::split(tokens, line, boost::algorithm::is_any_of(" ="), boost::algorithm::token_compress_on);
                if (tokens.empty())
                    continue;
                if (tokens.size() != 2)
                    throw std::runtime_error("Malformed string '" + line + "' in the config file '" + fn + "'");

                if (tokens.front() == "mysql_host")
                    mysql_host = tokens.back();
                else if (tokens.front() == "mysql_port")
                    mysql_port = atoi(tokens.back().c_str());
                else if (tokens.front() == "mysql_db")
                    mysql_db = tokens.back();
                else if (tokens.front() == "mysql_user")
                    mysql_user = tokens.back();
                else if (tokens.front() == "mysql_pass")
                    mysql_pass = tokens.back();
                else
                    throw std::runtime_error("unknown option '" + tokens.front() + "' in config file '" + fn + "'");
            }
        }
    };

    template <typename T>
    bool not_equal(const T& a, const T& b)
    {
        return a != b;
    }

    bool not_equal(double a, double b)
    {
        return fabs(a-b) > DBL_EPSILON * fmax(fabs(a),fabs(b));
    }

    template <typename T>
    class Atomic
    {
        volatile T m_Value;

    public:
        typedef T value_type;

        Atomic() {}
        Atomic(T aValue) : m_Value(aValue) {}
        Atomic(const Atomic& r) : m_Value(r) {} // uses operator T
        Atomic& operator= (const Atomic& r) { return operator = (static_cast<T>(r)); }

        T operator++ ()         { return __sync_add_and_fetch(&m_Value, 1); }
        T operator++ (int)      { return __sync_fetch_and_add(&m_Value, 1); }
        T operator+= (T aValue) { return __sync_add_and_fetch(&m_Value, aValue); }

        T operator-- ()         { return __sync_sub_and_fetch(&m_Value, 1); }
        T operator-- (int)      { return __sync_fetch_and_sub(&m_Value, 1); }
        T operator-= (T aValue) { return __sync_sub_and_fetch(&m_Value, aValue); }

        Atomic& operator= (T aValue) { __sync_lock_test_and_set(&m_Value, aValue); return *this; }

        operator T() const
        {
            __sync_synchronize();
            return m_Value;
        }
    };

    struct Fixture
    {
        struct TestExtState : public slave::ExtStateIface
        {
            std::mutex m_Mutex;
            std::condition_variable m_CondVariable;

            TestExtState() : intransaction_pos(0) {}

            virtual slave::State getState() { return slave::State(); }
            virtual void setConnecting() {}
            virtual time_t getConnectTime() { return 0; }
            virtual void setLastFilteredUpdateTime() {}
            virtual time_t getLastFilteredUpdateTime() { return 0; }
            virtual void setLastEventTimePos(time_t t, unsigned long pos) { intransaction_pos = pos; }
            virtual time_t getLastUpdateTime() { return 0; }
            virtual time_t getLastEventTime() { return 0; }
            virtual unsigned long getIntransactionPos() { return intransaction_pos; }
            virtual void setMasterPosition(const slave::Position& pos)
            {
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    position = pos;
                    intransaction_pos = pos.log_pos;
                }
                m_CondVariable.notify_one();
            }
            virtual void saveMasterPosition() {}
            virtual bool loadMasterPosition(slave::Position& pos) { pos.clear(); return false; }
            virtual bool getMasterPosition(slave::Position& pos)
            {
                if (!position.empty())
                {
                    pos = position;
                    if (intransaction_pos)
                        pos.log_pos = intransaction_pos;
                    return true;
                }
                return loadMasterPosition(pos);
            }
            virtual unsigned int getConnectCount() { return 0; }
            virtual void setStateProcessing(bool _state) {}
            virtual bool getStateProcessing() { return false; }
            virtual void initTableCount(const std::string& t) {}
            virtual void incTableCount(const std::string& t) {}

        private:
            slave::Position position;
            unsigned long   intransaction_pos;
        };

        class TestSlaveStat : public slave::EventStatIface
        {
        public:
            time_t   last_event_master_time  = 0;
            time_t   last_event_receive_time = 0;

            uint64_t events_total              = 0;
            uint64_t events_table_map          = 0;
            uint64_t events_format_description = 0;
            uint64_t events_query              = 0;
            uint64_t events_rotate             = 0;
            uint64_t events_xid                = 0;
            uint64_t events_other              = 0;
            uint64_t events_modify             = 0;
            uint64_t rows_modify               = 0;

            struct Counter
            {
                uint64_t ignored  = 0;
                uint64_t done     = 0;
                uint64_t failed   = 0;
                time_t   row_done = 0;
            };

            std::map<std::pair<std::string, std::string>, unsigned long>  map_table;
            std::map<slave::EventKind, Counter>                           map_kind;
            std::map<std::pair<unsigned long, slave::EventKind>, Counter> map_detailed;

            virtual void processTableMap(const unsigned long id, const std::string& table, const std::string& database)
            {
                const auto key = std::make_pair(database, table);
                map_table[key] = id;
                ++events_table_map;
            }

            virtual void tick(time_t when)
            {
                ++events_total;
                if (when != 0)
                {
                    last_event_master_time  = when;
                    last_event_receive_time = ::time(NULL);
                }
            }

            virtual void tickFormatDescription() { ++events_total; ++events_format_description; }

            virtual void tickQuery() { ++events_query; }

            virtual void tickRotate() { ++events_total; ++events_rotate; }

            virtual void tickXid() { ++events_xid; }

            virtual void tickOther() { ++events_other; }

            virtual void tickModifyEventIgnored(const unsigned long id, slave::EventKind kind)
            {
                ++events_modify;

                const auto key = std::make_pair(id, kind);
                map_kind[kind].ignored    += 1;
                map_detailed[key].ignored += 1;
            }

            virtual void tickModifyEventDone(const unsigned long id, slave::EventKind kind)
            {
                ++events_modify;

                const auto key = std::make_pair(id, kind);
                map_kind[kind].done    += 1;
                map_detailed[key].done += 1;
            }

            virtual void tickModifyEventFailed(const unsigned long id, slave::EventKind kind)
            {
                ++events_modify;

                const auto key = std::make_pair(id, kind);
                map_kind[kind].failed    += 1;
                map_detailed[key].failed += 1;
            }

            virtual void tickModifyRowDone(const unsigned long id, slave::EventKind kind, uint64_t time)
            {
                ++rows_modify;

                const auto key = std::make_pair(id, kind);
                map_kind[kind].row_done    += time;
                map_detailed[key].row_done += time;
            }
        };

        config cfg;
        TestExtState m_ExtState;
        TestSlaveStat m_SlaveStat;
        slave::Slave m_Slave;
        std::shared_ptr<nanomysql::Connection> conn;

        struct StopFlag
        {
            Atomic<int> m_StopFlag;
            Atomic<int> m_SlaveStarted;
            Atomic<int> m_SleepFlag;

            StopFlag()
            :   m_StopFlag(false)
            ,   m_SlaveStarted(false)
            ,   m_SleepFlag(false)
            {}

            bool operator() ()
            {
                m_SlaveStarted = true;
                if (m_SleepFlag)
                {
                    ::sleep(1);
                    m_SleepFlag = false;
                }
                return m_StopFlag;
            }
        };

        StopFlag        m_StopFlag;
        std::thread     m_SlaveThread;

        struct Callback
        {
            std::mutex m_Mutex;
            slave::callback m_Callback;
            Atomic<int> m_UnwantedCalls;

            Callback() : m_UnwantedCalls(0) {}

            void operator() (slave::RecordSet& rs)
            {
                std::lock_guard<std::mutex> l(m_Mutex);
                if (m_Callback)
                    m_Callback(rs);
                else
                    ++m_UnwantedCalls;
            }

            void setCallback(slave::callback c)
            {
                std::lock_guard<std::mutex> l(m_Mutex);
                m_Callback = c;
            }

            void setCallback()
            {
                std::lock_guard<std::mutex> l(m_Mutex);
                m_Callback = nullptr;
            }
        };

        Callback m_Callback;
        slave::EventKind m_Filter;

        void startSlave()
        {
            m_StopFlag.m_StopFlag = false;

            m_Slave.createDatabaseStructure();

            // Run libslave with our custom stop-function, which also signals
            // when slave has read binlog position and is ready to get messages.
            m_SlaveThread = std::thread([this] ()
            {
                m_Slave.get_remote_binlog(std::ref(m_StopFlag));
                mysql_thread_end();
            });

            // Wait libslave to run - no more than 1000 times with 1 ms.
            const timespec ts = {0 , 1000000};
            size_t i = 0;
            for (; i < 1000; ++i)
            {
                ::nanosleep(&ts, NULL);
                if (m_StopFlag.m_SlaveStarted)
                    break;
            }
            if (1000 == i)
                BOOST_FAIL ("Can't connect to mysql via libslave in 1 second");
        }

        Fixture(slave::EventKind filter = slave::eAll) : m_Slave(m_ExtState), m_Filter(filter)
        {
            cfg.load(TestDataDir + "mysql.conf");

            slave::MasterInfo sMasterInfo;
            sMasterInfo.conn_options.mysql_host = cfg.mysql_host;
            sMasterInfo.conn_options.mysql_port = cfg.mysql_port;
            sMasterInfo.conn_options.mysql_user = cfg.mysql_user;
            sMasterInfo.conn_options.mysql_pass = cfg.mysql_pass;
            sMasterInfo.conn_options.mysql_db = cfg.mysql_db;

            conn.reset(new nanomysql::Connection(sMasterInfo.conn_options));
            conn->query("set names utf8");
            conn->query("set time_zone='+3:00'");
            // Create table, because if it does not exist, libslave will swear the lack of it, and test will finished.
            conn->query("CREATE TABLE IF NOT EXISTS test (tmp int)");
            // Create another table for testing map_detailed stat.
            conn->query("CREATE TABLE IF NOT EXISTS stat (tmp int)");

            m_Slave.setMasterInfo(sMasterInfo);
            m_Slave.linkEventStat(&m_SlaveStat);
            // Set callback into Fixture - and it will call callbacks which will be set in tests.
            m_Slave.setCallback(cfg.mysql_db, "test", std::ref(m_Callback), filter);
            m_Slave.setCallback(cfg.mysql_db, "stat", std::ref(m_Callback), filter);
            m_Slave.init();
            startSlave();
        }

        void stopSlave()
        {
            m_StopFlag.m_StopFlag = true;
            m_Slave.close_connection();
            if (m_SlaveThread.joinable())
                m_SlaveThread.join();
        }

        ~Fixture()
        {
            stopSlave();
        }

        template<typename T>
        struct Collector
        {
            typedef slave::RecordSet::TypeEvent TypeEvent;
            typedef boost::optional<T> Row;
            typedef std::tuple<TypeEvent, Row, Row> Event;
            typedef std::vector<Event> EventVector;
            EventVector data;

            static Row extract(const slave::Row& row)
            {
                if (row.size() > 1)
                {
                    std::ostringstream str;
                    str << "Row size is " << row.size();
                    throw std::runtime_error(str.str());
                }
                const slave::Row::const_iterator it = row.find("value");
                if (row.end() != it)
                    return boost::any_cast<T>(it->second.second);
                else
                    return Row();
            }

            void operator()(const slave::RecordSet& rs)
            {
                data.emplace_back(std::make_tuple(rs.type_event, extract(rs.m_old_row), extract(rs.m_row)));
            }

            static void expectNothing(const Row& row, const std::string& name,
                                      const std::string& aErrorMessage)
            {
                if (row)
                    BOOST_ERROR("Has " << name << " image with '" << row.get()
                                << "' value, expected nothing during" << aErrorMessage);
            }

            static void expectValue(const T& value, const Row& row, const std::string& name,
                                    const std::string& aErrorMessage)
            {
                if (!row)
                    BOOST_ERROR("Has not " << name << " image, expected '" << value << "' during" << aErrorMessage);
                if (not_equal(row.get(), value))
                    BOOST_ERROR("Has invalid " << name << " image with '" << row.get() << "'"
                                << "while expected '"<< value << "' during " << aErrorMessage);
            }

            static void expectEventType(const TypeEvent& expected, const TypeEvent& value, const std::string& name,
                                        const std::string& aErrorMessage)
            {
                if (not_equal(expected, value))
                    BOOST_ERROR("Has invalid " << name << " image with '" << value << "'"
                                << "while expected '"<< expected << "' during " << aErrorMessage);
            }

            void checkInsert(const T& t, const std::string& aErrorMessage) const
            {
                if (data.size() != 1)
                    BOOST_ERROR ("Have invalid call count: " << data.size() << " for " << aErrorMessage);
                const auto& tuple = data.front();
                expectEventType(TypeEvent::Write, std::get<0>(tuple), "TYPE_EVENT", aErrorMessage);
                expectNothing(std::get<1>(tuple), "BEFORE", aErrorMessage);
                expectValue(t, std::get<2>(tuple), "AFTER", aErrorMessage);
            }

            void checkUpdate(const T& was, const T& now, const std::string& aErrorMessage) const
            {
                if (data.size() != 1)
                    BOOST_ERROR ("Have invalid call count: " << data.size() << " for " << aErrorMessage);
                const auto& tuple = data.front();
                expectEventType(TypeEvent::Update, std::get<0>(tuple), "TYPE_EVENT", aErrorMessage);
                expectValue(was, std::get<1>(tuple), "BEFORE", aErrorMessage);
                expectValue(now, std::get<2>(tuple), "AFTER", aErrorMessage);
            }

            void checkDelete(const T& was, const std::string& aErrorMessage) const
            {
                if (data.size() != 1)
                    BOOST_ERROR ("Have invalid call count: " << data.size() << " for " << aErrorMessage);
                const auto& tuple = data.front();
                expectEventType(TypeEvent::Delete, std::get<0>(tuple), "TYPE_EVENT", aErrorMessage);
                expectValue(was, std::get<2>(tuple), "BEFORE", aErrorMessage);
                expectNothing(std::get<1>(tuple), "AFTER", aErrorMessage);
            }

            void checkNothing(const std::string& aErrorMessage)
            {
                if (!data.empty())
                    BOOST_ERROR ("Have invalid call count: " << data.size() << " for " << aErrorMessage);
            }
        };

        void waitCall()
        {
            slave::Position pos;
            conn->query("SHOW MASTER STATUS");
            conn->use([&pos](const nanomysql::fields_t& row)
            {
                pos.log_name = row.at("File").data;
                pos.log_pos = std::stoul(row.at("Position").data);
                auto it = row.find("Executed_Gtid_Set");
                if (it != row.end())
                    pos.parseGtid(it->second.data);
            });

            std::unique_lock<std::mutex> lock(m_ExtState.m_Mutex);
            if (!m_ExtState.m_CondVariable.wait_for(lock, std::chrono::milliseconds(2000), [this, &pos]
            {
                slave::Position posTmp;
                m_ExtState.getMasterPosition(posTmp);
                return posTmp.reachedOtherPos(pos);
            }))
                BOOST_ERROR("Condition variable timed out");
        }

        template<typename T>
        bool waitCall(const Collector<T>& aCallback)
        {
            waitCall();

            if (aCallback.data.empty())
                return false;
            return true;
        }

        bool shouldProcess(slave::EventKind filter, slave::EventKind sort)
        {
            return (filter & sort) == sort;
        }

        template<typename T, typename F>
        void check(F f, const std::string& aQuery, const std::string& aErrorMsg, slave::EventKind sort)
        {
            // Set callback in libslave for checking the value.
            Collector<T> sCallback;
            m_Callback.setCallback(std::ref(sCallback));
            // Check the lack of unwanted calls before the case.
            if (0 != m_Callback.m_UnwantedCalls)
                BOOST_ERROR("Unwanted calls before this case: " << m_Callback.m_UnwantedCalls << aErrorMsg);

            // Modify table.
            conn->query(aQuery);

            if (waitCall(sCallback))
            {
                if (shouldProcess(m_Filter, sort))
                    f(sCallback);
                else
                    BOOST_ERROR("Have unfiltered calls to libslave callback");
            }
            else
            {
                if (shouldProcess(m_Filter, sort))
                    BOOST_ERROR("Have no calls to libslave callback");
            }

            // Reset our callback, because exiting from scope it will destroy, and at the same time
            // in order to avoid its touching the string while we check the string.
            m_Callback.setCallback();
        }

        template<typename T>
        struct Line
        {
            std::string type;
            std::string filename;
            std::string line;
            size_t      lineNumber;
            std::string insert;
            T           expected;
        };

        template<typename T> static std::string errorMessage(const Line<T>& c)
        {
            return "(we are now on file '" + c.filename + "' line " + std::to_string(c.lineNumber) + ": '" + c.line + "')";
        }

        template <typename T>
        void checkInsertValue(T t, const std::string& aValue, const std::string& aErrorMessage, const std::string& aTable = "test")
        {
            check<T>([&t, &aErrorMessage](const Collector<T>& collector)
                     { collector.checkInsert(t, aErrorMessage); },
                     "INSERT INTO " + aTable + " VALUES (" + aValue + ")", aErrorMessage, slave::eInsert);
        }

        template<typename T> void checkInsert(const Line<T>& line)
        {
            checkInsertValue<T>(line.expected, line.insert, errorMessage(line));
        }

        template<typename T>
        void checkUpdate(Line<T> was, Line<T> now)
        {
            check<T>([&was, &now](const Collector<T>& collector)
                     { collector.checkUpdate(was.expected, now.expected, errorMessage(now)); },
                     "UPDATE test SET value=" + now.insert, errorMessage(now), slave::eUpdate);
        }

        template <typename T>
        void checkDeleteValue(T was, const std::string& aValue, const std::string& aErrorMessage)
        {
            check<T>([&was, &aErrorMessage](const Collector<T>& collector)
                     { collector.checkDelete(was, aErrorMessage); },
                     "DELETE FROM test", aErrorMessage, slave::eDelete);
        }

        template<typename T> void recreate(std::shared_ptr<nanomysql::Connection>& conn,
                                           const Line<T>& c)
        {
            const std::string sDropTableQuery = "DROP TABLE IF EXISTS test";
            conn->query(sDropTableQuery);
            const std::string sCreateTableQuery = "CREATE TABLE test (value " + c.type + ") DEFAULT CHARSET=utf8";
            conn->query(sCreateTableQuery);
        }

        template<typename T> void testInsert(std::shared_ptr<nanomysql::Connection>& conn,
                                             const std::vector<Line<T>>& data)
        {
            for (const Line<T>& c : data)
            {
                recreate(conn, c);
                checkInsertValue<T>(c.expected, c.insert, errorMessage(c));
            }
        }

        template<typename T> void testUpdate(std::shared_ptr<nanomysql::Connection>& conn,
                                             const std::vector<Line<T>>& data)
        {
            for (std::size_t i = 0; i < data.size(); ++i)
                if (i == 0)
                {
                    recreate(conn, data[0]);
                    checkInsert<T>(data[0]);
                }
                else
                {
                    // IF statement here, because otherwise callback will not trigger.
                    if (data[i-1].expected != data[i].expected)
                        checkUpdate<T>(data[i-1], data[i]);
                }
            if (data.back().expected != data.front().expected)
                checkUpdate<T>(data.back(), data.front());
        }

        template<typename T> void testDelete(std::shared_ptr<nanomysql::Connection>& conn,
                                             const std::vector<Line<T>>& data)
        {
            for (const Line<T>& c : data)
            {
                recreate(conn, c);
                checkInsertValue<T>(c.expected, c.insert, errorMessage(c));

                checkDeleteValue<T>(c.expected, c.insert, errorMessage(c));
            }
        }

        template<typename T> void testAll(std::shared_ptr<nanomysql::Connection>& conn,
                                          const std::vector<Line<T>>& data)
        {
            if (data.empty())
                return;
            testInsert(conn, data);
            testUpdate(conn, data);
            testDelete(conn, data);
        }
    };


    void test_HelloWorld()
    {
        std::cout << "You probably should specify parameters to mysql in the file " << TestDataDir << "mysql.conf first" << std::endl;
    }

    // Check, if stop slave, in the future it will continue reading from the same position.
    void test_StartStopPosition()
    {
        Fixture f;
        // Create needed table.
        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        f.checkInsertValue(uint32_t(12321), "12321", "");

        f.stopSlave();

        f.conn->query("INSERT INTO test VALUES (345234)");

        Fixture::Collector<uint32_t> sCallback;
        f.m_Callback.setCallback(std::ref(sCallback));

        f.startSlave();

        auto sErrorMessage = "start/stop test";
        if (!f.waitCall(sCallback))
            BOOST_ERROR("Have no calls to libslave callback for " << sErrorMessage);
        sCallback.checkInsert(345234, sErrorMessage);

        // Reset our callback, because exiting from scope it will destroy, and at the same time
        // in order to avoid its touching the string while we check the string.
        f.m_Callback.setCallback();

        // Check the lack of unwanted calls before the case.
        if (0 != f.m_Callback.m_UnwantedCalls)
            BOOST_ERROR("Unwanted calls before this case: " << f.m_Callback.m_UnwantedCalls);
    }

    struct CheckBinlogPos
    {
        const slave::Slave& m_Slave;
        slave::Position     m_LastPos;

        CheckBinlogPos(const slave::Slave& aSlave, const slave::Position& aLastPos)
        :   m_Slave(aSlave), m_LastPos(aLastPos)
        {}

        bool operator() () const
        {
            const slave::MasterInfo& sMasterInfo = m_Slave.masterInfo();
            return sMasterInfo.position.reachedOtherPos(m_LastPos);
        }
    };

    struct CallbackCounter
    {
        Atomic<int> counter;
        std::string fail;

        CallbackCounter() : counter(0) {}

        void operator() (const slave::RecordSet& rs)
        {
            if (++counter > 2)
                fail = std::to_string(counter) + " calls on CallbackCounter";
        }
    };

    // Check whether manual setting of binlog position works.
    void test_SetBinlogPos()
    {
        Fixture f;
        // Create needed table.
        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        f.checkInsertValue(uint32_t(12321), "12321", "");

        // Remember position.
        const auto sInitialBinlogPos = f.m_Slave.getLastBinlogPos();

        // Insert value, read it.
        f.checkInsertValue(uint32_t(12322), "12322", "");

        f.stopSlave();

        // Insert new value.
        f.conn->query("INSERT INTO test VALUES (345234)");

        // And get new position.
        const auto sCurBinlogPos = f.m_Slave.getLastBinlogPos();
        BOOST_CHECK_NE(sCurBinlogPos.log_pos, sInitialBinlogPos.log_pos);

        // Now set old position in slave and check that 2 INSERTs will have been read (12322 and 345234).
        slave::MasterInfo sMasterInfo = f.m_Slave.masterInfo();
        sMasterInfo.position = sInitialBinlogPos;
        f.m_Slave.setMasterInfo(sMasterInfo);

        CallbackCounter sCallback;
        f.m_Callback.setCallback(std::ref(sCallback));
        if (0 != f.m_Callback.m_UnwantedCalls)
        {
            BOOST_ERROR("Unwanted calls before this case: " << f.m_Callback.m_UnwantedCalls);
        }

        f.m_SlaveThread = std::thread([&f, sCurBinlogPos] ()
        {
            f.m_Slave.get_remote_binlog(CheckBinlogPos(f.m_Slave, sCurBinlogPos));
            mysql_thread_end();
        });

        // Wait callback triggering no more than 1 second.
        const timespec ts = {0 , 1000000};
        size_t i = 0;
        for (; i < 1000; ++i)
        {
            ::nanosleep(&ts, NULL);
            if (sCallback.counter >= 2)
                break;
        }
        if (sCallback.counter < 2)
            BOOST_ERROR ("Have less than two calls to libslave callback for 1 second");

        // Reset our callback, because exiting from scope it will destroy, and at the same time
        // in order to avoid its touching the string while we check the string.
        f.m_Callback.setCallback();

        if (!sCallback.fail.empty())
            BOOST_ERROR(sCallback.fail);

        BOOST_CHECK_MESSAGE (f.m_SlaveThread.joinable(), "m_Slave.get_remote_binlog is not finished yet and will be never!");
    }

    // Check, if connection to db loses (without exit from get_remote_binlog), then start reading from position where stopped.
    void test_Disconnect()
    {
        Fixture f;
        // Create needed table.
        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        f.checkInsertValue(uint32_t(12321), "12321", "");

        f.m_StopFlag.m_SleepFlag = true;
        f.m_Slave.close_connection();

        f.conn->query("INSERT INTO test VALUES (345234)");

        Fixture::Collector<uint32_t> sCallback;
        f.m_Callback.setCallback(std::ref(sCallback));

        auto sErrorMessage = "disconnect test";
        if (!f.waitCall(sCallback))
            BOOST_ERROR("Have no calls to libslave callback for " << sErrorMessage);
        sCallback.checkInsert(345234, sErrorMessage);

        // Reset our callback, because exiting from scope it will destroy, and at the same time
        // in order to avoid its touching the string while we check the string.
        f.m_Callback.setCallback();

        // Check the lack of unwanted calls before the case.
        if (0 != f.m_Callback.m_UnwantedCalls)
            BOOST_ERROR("Unwanted calls before this case: " << f.m_Callback.m_UnwantedCalls);
    }

    enum MYSQL_TYPE
    {
        MYSQL_TINYINT,
        MYSQL_INT,
        MYSQL_BIGINT,
        MYSQL_CHAR,
        MYSQL_VARCHAR,
        MYSQL_TINYTEXT,
        MYSQL_TEXT,
        MYSQL_DECIMAL,
        MYSQL_BIT,
        MYSQL_SET,
        MYSQL_TIMESTAMP,
        MYSQL_TIME,
        MYSQL_DATETIME,
        MYSQL_DATE,
        MYSQL_YEAR
    };

    template <MYSQL_TYPE T>
    struct MYSQL_type_traits;

    template <>
    struct MYSQL_type_traits<MYSQL_INT>
    {
        typedef slave::types::MY_INT slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_INT>::name = "INT";

    template <>
    struct MYSQL_type_traits<MYSQL_BIGINT>
    {
        typedef slave::types::MY_BIGINT slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_BIGINT>::name = "BIGINT";

    template <>
    struct MYSQL_type_traits<MYSQL_CHAR>
    {
        typedef slave::types::MY_CHAR slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_CHAR>::name = "CHAR";

    template <>
    struct MYSQL_type_traits<MYSQL_VARCHAR>
    {
        typedef slave::types::MY_VARCHAR slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_VARCHAR>::name = "VARCHAR";

    template <>
    struct MYSQL_type_traits<MYSQL_TINYTEXT>
    {
        typedef slave::types::MY_TINYTEXT slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_TINYTEXT>::name = "TINYTEXT";

    template <>
    struct MYSQL_type_traits<MYSQL_TEXT>
    {
        typedef slave::types::MY_TEXT slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_TEXT>::name = "TEXT";

    template <>
    struct MYSQL_type_traits<MYSQL_DECIMAL>
    {
        typedef slave::types::MY_DECIMAL slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_DECIMAL>::name = "DECIMAL";

    template <>
    struct MYSQL_type_traits<MYSQL_BIT>
    {
        typedef slave::types::MY_BIT slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_BIT>::name = "BIT";

    template <>
    struct MYSQL_type_traits<MYSQL_SET>
    {
        typedef slave::types::MY_SET slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_SET>::name = "SET";

    template <>
    struct MYSQL_type_traits<MYSQL_TIMESTAMP>
    {
        typedef slave::types::MY_TIMESTAMP slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_TIMESTAMP>::name = "TIMESTAMP";

    template <>
    struct MYSQL_type_traits<MYSQL_TIME>
    {
        typedef slave::types::MY_TIME slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_TIME>::name = "TIME";

    template <>
    struct MYSQL_type_traits<MYSQL_DATETIME>
    {
        typedef slave::types::MY_DATETIME slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_DATETIME>::name = "DATETIME";

    template <>
    struct MYSQL_type_traits<MYSQL_DATE>
    {
        typedef slave::types::MY_DATE slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_DATE>::name = "DATE";

    template <>
    struct MYSQL_type_traits<MYSQL_YEAR>
    {
        typedef slave::types::MY_TINYINT slave_type;
        static const std::string name;
    };
    const std::string MYSQL_type_traits<MYSQL_YEAR>::name = "YEAR";

    template <typename T>
    void getValue(const std::string& s, T& t)
    {
        std::istringstream is;
        is.str(s);
        is >> t;
    }

    void getValue(const std::string& s, std::string& t)
    {
        t = s;
        // Remove leading space.
        t.erase(0, 1);
    }

    template<typename T>
    void testOneType(Fixture& fixture)
    {
        typedef MYSQL_type_traits<MYSQL_TYPE(T::value)> type_traits;
        typedef typename type_traits::slave_type slave_type;

        const std::string sDataFilename = TestDataDir + "OneField/" + type_traits::name;
        std::ifstream f(sDataFilename.c_str());
        BOOST_REQUIRE_MESSAGE(f, "Cannot open file with data: '" << sDataFilename << "'");
        std::string line;
        size_t line_num = 0;
        std::vector<Fixture::Line<slave_type>> data;
        std::string type;
        while (getline(f, line))
        {
            ++line_num;
            if (line.empty())
                continue;
            std::vector<std::string> tokens;
            const char* sDelimiters = ",";
            if ("SET" == type_traits::name)
                sDelimiters=";";
            boost::algorithm::split(tokens, line, boost::algorithm::is_any_of(sDelimiters), boost::algorithm::token_compress_on);
            if (tokens.empty())
                continue;
            if (tokens.front() == "define")
            {
                if (tokens.size() > 2)
                {
                    std::string dec = tokens[1].substr(1, tokens[1].find('(', 0)-1);
                    if ("DECIMAL" == dec)
                    {
                        tokens[1] += "," + tokens[2];
                        tokens.pop_back();
                    }
                }
                if (tokens.size() != 2)
                    BOOST_FAIL("Malformed string '" << line << "' in the file '" << sDataFilename << "'");
                type = tokens[1];
                fixture.testAll(fixture.conn, data);
                data.clear();
            }
            else if (tokens.front() == "data")
            {
                if (tokens.size() != 3)
                    BOOST_FAIL("Malformed string '" << line << "' in the file '" << sDataFilename << "'");

                // Get value. Value from libslave must be compared with it.
                slave_type checked_value;
                getValue(tokens[2], checked_value);

                Fixture::Line<slave_type> current;
                current.type = type;
                current.filename = sDataFilename;
                current.line = line;
                current.lineNumber = line_num;
                current.insert = tokens[1];
                current.expected = checked_value;
                data.push_back(current);
            }
            else if (tokens.front()[0] == ';')
                continue;
            else
                BOOST_FAIL("Unknown command '" << tokens.front() << "' in the file '" << sDataFilename << "' on line " << line_num);
        }
        fixture.testAll(fixture.conn, data);
        data.clear();
    }

    void testOneFilter(slave::EventKind filter)
    {
        Fixture f(filter);
        testOneType<boost::mpl::int_<MYSQL_INT>>(f);
    }

    void testOneFilterAllTypes(slave::EventKind filter)
    {
        Fixture f(filter);
        testOneType<boost::mpl::int_<MYSQL_INT>>(f);
        testOneType<boost::mpl::int_<MYSQL_BIGINT>>(f);
        testOneType<boost::mpl::int_<MYSQL_CHAR>>(f);
        testOneType<boost::mpl::int_<MYSQL_VARCHAR>>(f);
        testOneType<boost::mpl::int_<MYSQL_TINYTEXT>>(f);
        testOneType<boost::mpl::int_<MYSQL_TEXT>>(f);
        testOneType<boost::mpl::int_<MYSQL_DECIMAL>>(f);
        testOneType<boost::mpl::int_<MYSQL_BIT>>(f);
        testOneType<boost::mpl::int_<MYSQL_SET>>(f);
        testOneType<boost::mpl::int_<MYSQL_TIMESTAMP>>(f);
        testOneType<boost::mpl::int_<MYSQL_TIME>>(f);
        testOneType<boost::mpl::int_<MYSQL_DATETIME>>(f);
        testOneType<boost::mpl::int_<MYSQL_DATE>>(f);
        testOneType<boost::mpl::int_<MYSQL_YEAR>>(f);
    }

    void testStatOneFilter(slave::EventKind filter)
    {
        for (auto& sKindRun: slave::eventKindList())
        {
            Fixture f(filter);

            f.conn->query("DROP TABLE IF EXISTS test");
            f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");
            f.conn->query("INSERT INTO test VALUES (1)");

            std::string sQuery;
            switch (sKindRun)
            {
            case slave::eInsert:
                sQuery = "INSERT INTO test VALUES (9)";
                break;
            case slave::eUpdate:
                sQuery = "UPDATE test SET value=2 WHERE value=1";
                break;
            default:
                sQuery = "DELETE FROM test WHERE value=1";
                break;
            }

            f.conn->query(sQuery);
            f.waitCall();

            if (f.m_Slave.masterVersion() < 50700)
                BOOST_CHECK_EQUAL(f.m_SlaveStat.events_total,              12);
            else
                BOOST_CHECK_EQUAL(f.m_SlaveStat.events_total,              16);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_table_map,          2);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_format_description, 1);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_query,              4);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_rotate,             1);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_xid,                2);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_modify,             2);

            auto sPair = std::make_pair(f.cfg.mysql_db, "test");
            if (f.m_SlaveStat.map_table.find(sPair) == f.m_SlaveStat.map_table.end())
                BOOST_ERROR("map_table key does not exist");
            const auto id = f.m_SlaveStat.map_table[sPair];

            for (auto& kind: slave::eventKindList())
            {
                const auto key = std::make_pair(id, kind);

                uint64_t sFlag             = static_cast<uint64_t>(sKindRun == kind);
                uint64_t sShouldNotProcess = static_cast<uint64_t>(!f.shouldProcess(filter, kind));
                if (kind == slave::eInsert)
                    ++sFlag;

                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].ignored,    sShouldNotProcess * sFlag);
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].ignored, sShouldNotProcess * sFlag);

                if (sShouldNotProcess == 0 && (kind == sKindRun || kind == slave::eInsert))
                {
                    BOOST_CHECK_GT(   f.m_SlaveStat.map_kind[kind].done,    0);
                    BOOST_CHECK_GT(   f.m_SlaveStat.map_detailed[key].done, 0);
                    BOOST_CHECK_GT(   f.m_SlaveStat.map_kind[kind].row_done,    0);
                    BOOST_CHECK_GT(   f.m_SlaveStat.map_detailed[key].row_done, 0);
                }
                else
                {
                    BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].done,    0);
                    BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].done, 0);
                    BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].row_done,    0);
                    BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].row_done, 0);
                }

                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].failed,  0);
            }
        }
    }

    struct CallbackFailedEvent
    {
        void operator() (const slave::RecordSet& rs)
        {
            throw std::exception();
        }
    };

    void testFailedEvents()
    {
        Fixture f;

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        CallbackFailedEvent sCallback;
        f.m_Callback.setCallback(std::ref(sCallback));

        f.conn->query("INSERT INTO test VALUES (345234)");
        f.waitCall();

        auto sPair = std::make_pair(f.cfg.mysql_db, "test");
        if (f.m_SlaveStat.map_table.find(sPair) == f.m_SlaveStat.map_table.end())
            BOOST_ERROR("map_table key does not exist");
        const auto id = f.m_SlaveStat.map_table[sPair];

        for (auto& kind: slave::eventKindList())
        {
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].done,    0);

            const auto key = std::make_pair(id, kind);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].done,    0);

            if (kind == slave::eInsert)
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].failed,    1);
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].failed, 1);
            }
            else
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].failed,    0);
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].failed, 0);
            }
        }

        // Reset our callback.
        f.m_Callback.setCallback();

        // Check the lack of unwanted calls.
        if (0 != f.m_Callback.m_UnwantedCalls)
            BOOST_ERROR("Unwanted calls before this case: " << f.m_Callback.m_UnwantedCalls);
    }

    void testTableMapAndQueryEvents()
    {
        Fixture f;

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        f.conn->query("INSERT INTO test VALUES (1)");
        f.waitCall();
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_table_map, 1);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_query,     3);

        f.conn->query("DELETE FROM test WHERE value=1");
        f.waitCall();
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_table_map, 2);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_query,     4);

        f.conn->query("ALTER TABLE test ADD COLUMN tmp INT");
        f.conn->query("INSERT INTO test VALUES (1, 2)");
        f.waitCall();
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_table_map, 3);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_query,     6);

        f.conn->query("DROP TABLE test");
        f.conn->query("CREATE TABLE test (value int)");
        f.conn->query("INSERT INTO test VALUES (1)");
        f.conn->query("UPDATE test SET value=2 WHERE value=1");
        f.conn->query("DELETE FROM test WHERE value=2");
        f.waitCall();
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_table_map, 6);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_query,     11);
    }

    void testXidEvents()
    {
        Fixture f;

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        f.conn->query("START TRANSACTION");
        f.conn->query("INSERT INTO test VALUES (1)");
        f.conn->query("COMMIT");

        f.conn->query("START TRANSACTION");
        f.conn->query("INSERT INTO test VALUES (2)");
        f.conn->query("UPDATE test SET value=3 WHERE value=1");
        f.conn->query("DELETE FROM test WHERE value=2");
        f.conn->query("COMMIT");

        f.conn->query("UPDATE test SET value=4 WHERE value=3");
        f.conn->query("DELETE FROM test WHERE value=4");

        f.waitCall();

        if (f.m_Slave.masterVersion() < 50700)
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_total,              24);
        else
            BOOST_CHECK_EQUAL(f.m_SlaveStat.events_total,              30);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_table_map,          6);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_format_description, 1);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_query,              6);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_rotate,             1);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_xid,                4);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_modify,             6);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.rows_modify,               6);

        auto sPair = std::make_pair(f.cfg.mysql_db, "test");
        if (f.m_SlaveStat.map_table.find(sPair) == f.m_SlaveStat.map_table.end())
            BOOST_ERROR("map_table key does not exist");
        const auto id = f.m_SlaveStat.map_table[sPair];

        for (auto& kind: slave::eventKindList())
        {
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].done,    2);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_kind[kind].failed,  0);

            const auto key = std::make_pair(id, kind);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].done,    2);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[key].failed,  0);
        }
    }

    void testMapDetailed()
    {
        Fixture f;

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");
        f.conn->query("DROP TABLE IF EXISTS stat");
        f.conn->query("CREATE TABLE IF NOT EXISTS stat (value int)");

        f.conn->query("INSERT INTO stat VALUES (1)");
        f.waitCall();

        auto sPairStat = std::make_pair(f.cfg.mysql_db, "stat");
        if (f.m_SlaveStat.map_table.find(sPairStat) == f.m_SlaveStat.map_table.end())
            BOOST_ERROR("map_table key does not exist");

        auto sIdStat = f.m_SlaveStat.map_table[sPairStat];

        for (auto& kind: slave::eventKindList())
        {
            const auto sKeyStat = std::make_pair(sIdStat, kind);

            if (kind == slave::eInsert)
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].done,     1);
                BOOST_CHECK_GT(   f.m_SlaveStat.map_detailed[sKeyStat].row_done, 0);
            }
            else
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].done,     0);
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].row_done, 0);
            }

            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].failed,  0);
        }

        f.conn->query("INSERT INTO test VALUES (1)");
        f.conn->query("UPDATE test SET value=2 WHERE value=1");
        f.waitCall();

        auto sPairTest = std::make_pair(f.cfg.mysql_db, "test");
        if (f.m_SlaveStat.map_table.find(sPairTest) == f.m_SlaveStat.map_table.end())
            BOOST_ERROR("map_table key does not exist");

        auto sIdTest = f.m_SlaveStat.map_table[sPairTest];

        for (auto& kind: slave::eventKindList())
        {
            const auto sKeyTest = std::make_pair(sIdTest, kind);
            const auto sKeyStat = std::make_pair(sIdStat, kind);

            if (kind == slave::eInsert)
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].done,     1);
                BOOST_CHECK_GT(   f.m_SlaveStat.map_detailed[sKeyStat].row_done, 0);
            }
            else
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].done,     0);
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].row_done, 0);
            }

            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyStat].failed,  0);

            if (kind == slave::eDelete)
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyTest].done,     0);
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyTest].row_done, 0);
            }
            else
            {
                BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyTest].done,     1);
                BOOST_CHECK_GT(   f.m_SlaveStat.map_detailed[sKeyTest].row_done, 0);
            }

            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyTest].ignored, 0);
            BOOST_CHECK_EQUAL(f.m_SlaveStat.map_detailed[sKeyTest].failed,  0);
        }
    }

    void testLastEventTime()
    {
        Fixture f;

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");

        f.conn->query("INSERT INTO test VALUES (345234)");
        f.waitCall();

        BOOST_CHECK_LE(f.m_SlaveStat.last_event_master_time, f.m_SlaveStat.last_event_receive_time);
    }

    void testRowsModify()
    {
        Fixture f;

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");
        f.conn->query("INSERT INTO test VALUES (345234),(345235),(345236),(345237)");
        f.conn->query("UPDATE test SET value=0");
        f.waitCall();

        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_modify, 2);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.rows_modify,   8);

        f.conn->query("DELETE FROM test");
        f.waitCall();

        BOOST_CHECK_EQUAL(f.m_SlaveStat.events_modify, 3);
        BOOST_CHECK_EQUAL(f.m_SlaveStat.rows_modify,   12);
    }

    void test_Stat()
    {
        testStatOneFilter(slave::eAll);
        testStatOneFilter(slave::eInsert);
        testStatOneFilter(slave::eUpdate);
        testStatOneFilter(slave::eDelete);
        testStatOneFilter(slave::eNone);
        testStatOneFilter(static_cast<slave::EventKind>(~static_cast<uint8_t>(slave::eInsert)));
        testStatOneFilter(static_cast<slave::EventKind>(~static_cast<uint8_t>(slave::eUpdate)));
        testStatOneFilter(static_cast<slave::EventKind>(~static_cast<uint8_t>(slave::eDelete)));

        testFailedEvents();
        testTableMapAndQueryEvents();
        testXidEvents();
        testMapDetailed();
        testLastEventTime();
        testRowsModify();
    }

    struct CallbackRowImage
    {
        CallbackRowImage() { reset(); }

        void operator() (const slave::RecordSet& rs)
        {
            if (not rs.m_old_row.empty())
            {
                old_row_empty = false;
                auto it = rs.m_old_row.find("id");
                if (it != rs.m_old_row.end())
                    id_old = boost::any_cast<uint32_t>(it->second.second);

                it = rs.m_old_row.find("value");
                if (it != rs.m_old_row.end())
                    value_old = boost::any_cast<uint32_t>(it->second.second);
            }
            auto it = rs.m_row.find("id");
            if (it != rs.m_row.end())
                id = boost::any_cast<uint32_t>(it->second.second);

            it = rs.m_row.find("value");
            if (it != rs.m_row.end())
                value = boost::any_cast<uint32_t>(it->second.second);
        }

        void reset()
        {
            old_row_empty = true;
            id_old = value_old = id = value = 0;
        }

        bool old_row_empty;
        uint32_t id_old;
        uint32_t value_old;
        uint32_t id;
        uint32_t value;
    };

    void test_BinlogRowImageOption()
    {
        Fixture f;

        bool row_image_minimal = false;
        if (f.m_Slave.masterVersion() >= 50602)
        {
            f.conn->query("SELECT @@GLOBAL.binlog_row_image as binlog_row_image");
            f.conn->use([&row_image_minimal](const nanomysql::fields_t& row)
            {
                row_image_minimal = row.at("binlog_row_image").data == "MINIMAL";
            });
        }

        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (id int, value int, PRIMARY KEY(id))");

        CallbackRowImage sCallback;
        f.m_Callback.setCallback(std::ref(sCallback));

        f.conn->query("INSERT INTO test VALUES (1, 345234)");
        f.waitCall();
        BOOST_CHECK_EQUAL(sCallback.old_row_empty, true);
        BOOST_CHECK_EQUAL(sCallback.id,    1);
        BOOST_CHECK_EQUAL(sCallback.value, 345234);
        sCallback.reset();

        f.conn->query("UPDATE test SET value=132133 WHERE id=1");
        f.waitCall();
        BOOST_CHECK_EQUAL(sCallback.old_row_empty, false);
        BOOST_CHECK_EQUAL(sCallback.id_old,    1);
        if (not row_image_minimal)
        {
            BOOST_CHECK_EQUAL(sCallback.value_old, 345234);
            BOOST_CHECK_EQUAL(sCallback.id,        1);
        }
        else
        {
            BOOST_CHECK_EQUAL(sCallback.value_old, 0);
            BOOST_CHECK_EQUAL(sCallback.id,        0);
        }
        BOOST_CHECK_EQUAL(sCallback.value,     132133);
        sCallback.reset();

        f.conn->query("DELETE FROM test WHERE id=1");
        f.waitCall();
        BOOST_CHECK_EQUAL(sCallback.old_row_empty, true);
        BOOST_CHECK_EQUAL(sCallback.id,        1);
        if (not row_image_minimal)
            BOOST_CHECK_EQUAL(sCallback.value,     132133);
        else
            BOOST_CHECK_EQUAL(sCallback.value,     0);

        f.m_Callback.setCallback();
        if (0 != f.m_Callback.m_UnwantedCalls)
            BOOST_ERROR("Unwanted calls before this case: " << f.m_Callback.m_UnwantedCalls);
    }

    void test_AlterCreateTable()
    {
        Fixture f;
        f.conn->query("DROP TABLE IF EXISTS test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value int)");
        f.checkInsertValue(uint32_t(12321), "12321", "");

        f.conn->query("ALTER TABLE test DROP COLUMN value, ADD COLUMN value varchar(50)");
        f.checkInsertValue(std::string("test_value_is_here"), "'test_value_is_here'", "");

        f.conn->query("ALTER TABLE test DROP COLUMN value, ADD COLUMN value int");
        f.checkInsertValue(uint32_t(123456), "123456", "");

        f.conn->query("DROP TABLE test");
        f.conn->query("CREATE TABLE IF NOT EXISTS test (value varchar(10))");
        f.checkInsertValue(std::string("TEST"), "'TEST'", "");

        f.conn->query("DROP TABLE test");
        f.conn->query("CREATE TABLE test (value int)");
        f.checkInsertValue(uint32_t(11), "11", "");

        f.conn->query("DROP TABLE IF EXISTS stat");
        f.conn->query("CREATE TABLE IF NOT EXISTS test.stat (value varchar(50))");
        f.checkInsertValue(std::string("test_value_is_here"), "'test_value_is_here'", "", "stat");

        f.conn->query("ALTER TABLE test.stat DROP COLUMN value, ADD COLUMN value int");
        f.checkInsertValue(uint32_t(12321), "12321", "", "stat");

        f.conn->query("DROP TABLE IF EXISTS stat");
        f.conn->query("CREATE TABLE IF NOT EXISTS `stat` (value int)");
        f.checkInsertValue(uint32_t(12), "12", "", "stat");

        f.conn->query("ALTER TABLE `test`.`stat` DROP COLUMN `value`, ADD COLUMN `value` varchar(50)");
        f.checkInsertValue(std::string("test_value_is_here"), "'test_value_is_here'", "", "stat");

        f.conn->query("DROP TABLE IF EXISTS stat");
        f.conn->query("CREATE TABLE IF NOT EXISTS `stat`\n(value varchar(50))");
        f.checkInsertValue(std::string("test_value_is_here"), "'test_value_is_here'", "", "stat");

        f.conn->query("ALTER TABLE `stat` \n    DROP COLUMN `value`,\n    ADD COLUMN value int");
        f.checkInsertValue(uint32_t(12), "12", "", "stat");
    }

    void test_GtidParsing()
    {
        slave::Position pos;
        const std::string uuidText = "24f7c945-c871-11e6-9461-0242ac110006";
        const std::string uuid = "24f7c945c87111e694610242ac110006";

        pos.parseGtid(uuidText + ":1");
        BOOST_CHECK(pos.gtid_executed.find(uuid) != pos.gtid_executed.end());
        const auto& ref1 = pos.gtid_executed[uuid];
        BOOST_CHECK_EQUAL(ref1.size(), 1);
        BOOST_CHECK(ref1.front() == slave::gtid_interval_t(1, 1));

        pos.clear();
        pos.parseGtid(uuidText + ":1-697");
        BOOST_CHECK(pos.gtid_executed.find(uuid) != pos.gtid_executed.end());
        const auto& ref2 = pos.gtid_executed[uuid];
        BOOST_CHECK_EQUAL(ref2.size(), 1);
        BOOST_CHECK(ref2.front() == slave::gtid_interval_t(1, 697));

        pos.clear();
        pos.parseGtid(uuidText + ":1-697:704:706-710");
        BOOST_CHECK(pos.gtid_executed.find(uuid) != pos.gtid_executed.end());
        const auto& ref3 = pos.gtid_executed[uuid];
        BOOST_CHECK_EQUAL(ref3.size(), 3);
        BOOST_CHECK(ref3.front() == slave::gtid_interval_t(1, 697));
        BOOST_CHECK(*std::next(ref3.begin()) == slave::gtid_interval_t(704, 704));
        BOOST_CHECK(ref3.back() == slave::gtid_interval_t(706, 710));

        pos.clear();
        const std::string uuidText2 = "ae00751a-cb5f-11e6-9d92-e03f490fd3db";
        const std::string uuid2 = "ae00751acb5f11e69d92e03f490fd3db";
        const std::string uuidText3 = "ae00751a-cb5f-11e6-9d92-e03f490fd3de";
        const std::string uuid3 = "ae00751acb5f11e69d92e03f490fd3de";
        pos.parseGtid(uuidText + ":1-697, \n" + uuidText2 + ":1-14, " + uuidText3 + ":34\n");
        BOOST_CHECK(pos.gtid_executed.find(uuid) != pos.gtid_executed.end());
        BOOST_CHECK(pos.gtid_executed.find(uuid2) != pos.gtid_executed.end());
        BOOST_CHECK(pos.gtid_executed.find(uuid3) != pos.gtid_executed.end());
        const auto& ref4 = pos.gtid_executed[uuid];
        const auto& ref5 = pos.gtid_executed[uuid2];
        const auto& ref6 = pos.gtid_executed[uuid3];
        BOOST_CHECK_EQUAL(ref4.size(), 1);
        BOOST_CHECK_EQUAL(ref5.size(), 1);
        BOOST_CHECK_EQUAL(ref6.size(), 1);
        BOOST_CHECK(ref4.front() == slave::gtid_interval_t(1, 697));
        BOOST_CHECK(ref5.front() == slave::gtid_interval_t(1, 14));
        BOOST_CHECK(ref6.front() == slave::gtid_interval_t(34, 34));
    }

    void test_GtidAdding()
    {
        slave::Position pos;
        const std::string uuidText = "24f7c945-c871-11e6-9461-0242ac110006";
        const std::string uuid = "24f7c945c87111e694610242ac110006";
        pos.parseGtid(uuidText + ":2-4");
        const auto& ref = pos.gtid_executed[uuid];

        pos.addGtid(slave::gtid_t(uuid, 6));
        BOOST_CHECK_EQUAL(ref.size(), 2);
        BOOST_CHECK(ref.front() == slave::gtid_interval_t(2, 4));
        BOOST_CHECK(ref.back() == slave::gtid_interval_t(6, 6));

        pos.addGtid(slave::gtid_t(uuid, 5));
        BOOST_CHECK_EQUAL(ref.size(), 1);
        BOOST_CHECK(ref.front() == slave::gtid_interval_t(2, 6));

        pos.addGtid(slave::gtid_t(uuid, 1));
        BOOST_CHECK_EQUAL(ref.size(), 1);
        BOOST_CHECK(ref.front() == slave::gtid_interval_t(1, 6));

        pos.addGtid(slave::gtid_t(uuid, 7));
        BOOST_CHECK_EQUAL(ref.size(), 1);
        BOOST_CHECK(ref.front() == slave::gtid_interval_t(1, 7));

        const std::string uuid2 = "ae00751acb5f11e69d92e03f490fd3db";
        pos.addGtid(slave::gtid_t(uuid2, 2));
        BOOST_CHECK_EQUAL(ref.size(), 1);
        BOOST_CHECK(ref.front() == slave::gtid_interval_t(1, 7));
        const auto& ref2 = pos.gtid_executed[uuid2];
        BOOST_CHECK_EQUAL(ref2.size(), 1);
        BOOST_CHECK(ref2.front() == slave::gtid_interval_t(2, 2));
    }
}// anonymous-namespace

test_suite* init_unit_test_suite(int argc, char* argv[])
{
#define ADD_FIXTURE_TEST(testFunction) \
    framework::master_test_suite().add(BOOST_TEST_CASE([&]() { testFunction(); }))

    ADD_FIXTURE_TEST(test_HelloWorld);
    ADD_FIXTURE_TEST(test_StartStopPosition);
    ADD_FIXTURE_TEST(test_SetBinlogPos);
    ADD_FIXTURE_TEST(test_Disconnect);
    ADD_FIXTURE_TEST(test_Stat);
    ADD_FIXTURE_TEST(test_BinlogRowImageOption);
    ADD_FIXTURE_TEST(test_AlterCreateTable);
    ADD_FIXTURE_TEST(test_GtidParsing);
    ADD_FIXTURE_TEST(test_GtidAdding);

#undef ADD_FIXTURE_TEST

    framework::master_test_suite().add(BOOST_TEST_CASE([&]() { testOneFilterAllTypes(slave::eAll); }));

#define ADD_FILTER_TEST(filter) \
    framework::master_test_suite().add(BOOST_TEST_CASE([&]() { testOneFilter(filter); }))

    ADD_FILTER_TEST(slave::eInsert);
    ADD_FILTER_TEST(slave::eUpdate);
    ADD_FILTER_TEST(slave::eDelete);
    ADD_FILTER_TEST(slave::eNone);
    ADD_FILTER_TEST(static_cast<slave::EventKind>(~static_cast<uint8_t>(slave::eInsert)));
    ADD_FILTER_TEST(static_cast<slave::EventKind>(~static_cast<uint8_t>(slave::eUpdate)));
    ADD_FILTER_TEST(static_cast<slave::EventKind>(~static_cast<uint8_t>(slave::eDelete)));

#undef ADD_FILTER_TEST

    return 0;
}
