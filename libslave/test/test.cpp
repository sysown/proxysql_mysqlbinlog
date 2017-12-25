
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <signal.h>

#include "Slave.h"
#include "DefaultExtState.h"


volatile sig_atomic_t stop = 0;
slave::Slave* sl = NULL;

std::string print(const std::string& type, const boost::any& v) {

    if (v.type() == typeid(std::string)) {

        std::string r = "'";
        r += boost::any_cast<std::string>(v);
        r += "'";
        return r;

    } else {
        std::ostringstream s;

        if (v.type() == typeid(int))
            s << boost::any_cast<int>(v);

        else if (v.type() == typeid(unsigned int))
            s << boost::any_cast<unsigned int>(v);

        else if (v.type() == typeid(double))
            s << boost::any_cast<double>(v);

        else if (v.type() == typeid(unsigned long long))
            s << boost::any_cast<unsigned long long>(v);

        else if (v.type() == typeid(void))
            s << "void";

        else
            s << boost::any_cast<long>(v);

        return s.str();
    }
}


void callback(const slave::RecordSet& event) {

    slave::Position sBinlogPos = sl->getLastBinlogPos();
    std::cout << "master pos " << sBinlogPos << std::endl;

    switch (event.type_event) {
    case slave::RecordSet::Update: std::cout << "UPDATE"; break;
    case slave::RecordSet::Delete: std::cout << "DELETE"; break;
    case slave::RecordSet::Write:  std::cout << "INSERT"; break;
    default: break;
    }

    std::cout << " " << event.db_name << "." << event.tbl_name << "\n";

    for (slave::Row::const_iterator i = event.m_row.begin(); i != event.m_row.end(); ++i) {

        std::string value = print(i->second.first, i->second.second);

        std::cout << "  " << i->first << " : " << i->second.first << " -> " << value;

        if (event.type_event == slave::RecordSet::Update) {

            slave::Row::const_iterator j = event.m_old_row.find(i->first);

            std::string old_value("NULL");

            if (j != event.m_old_row.end())
                old_value = print(i->second.first, j->second.second);

            if (value != old_value)
                std::cout << "    (was: " << old_value << ")";
        }

        std::cout << "\n";
    }

    std::cout << "  @ts = "  << event.when << "\n"
         << "  @server_id = " << event.master_id << "\n\n";
}

void empty_callback(const slave::RecordSet& event) {
}

void xid_callback(unsigned int server_id) {

    std::cout << "COMMIT @server_id = " << server_id << "\n\n";
}

void empty_xid_callback(unsigned int server_id) {
}

// for benchmarks
size_t total_events;
size_t total_commits;
std::map<time_t, size_t> events_in_time;
size_t ev_counter;
std::map<time_t, size_t> commits_in_time;
size_t ci_counter;
time_t last_output_time;
double total_work_time;

void bench_callback(const slave::RecordSet& event) {
    ++total_events;
    ++ev_counter;
    if (ev_counter >= 100)
    {
        const time_t ct = ::time(NULL);
        events_in_time[ct] += ev_counter;
        ev_counter = 0;
        if (ct != last_output_time)
        {
            std::cerr << "\rRead " << total_events << " events";
            last_output_time = ct;
        }
    }
}

void bench_xid_callback(unsigned int server_id) {
    ++total_commits;
    ++ci_counter;
    if (ci_counter >= 100)
    {
        commits_in_time[::time(NULL)] += ci_counter;
        ci_counter = 0;
    }
}

void sighandler(int sig)
{
    stop = 1;
    sl->close_connection();
}

bool isStopping()
{
    return stop;
}

void usage(const char* name)
{
    std::cout << "Usage: " << name << " -h <mysql host> -u <mysql user> -p <mysql password> -d <mysql database>"
              << " -P <mysql port> [-b <binlog_name> -o <binlog_pos> -B <to_binlog_name> -O <to_binlog_pos|-g <gtid_pos>"
              << " -G <to_gtid_pos>] -C -m"
              << " <table name> <table name> ...\n"
              << " -C means use empty callbacks\n"
              << " -m means benchmark\n"
              << " If -C and -m both specified, then empty callbacks will be used, but logging will be switched off"
              << std::endl;
}

int main(int argc, char** argv)
{
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    unsigned int port = 3306;

    std::string binlog_name;
    unsigned long binlog_pos = 0;
    std::string to_binlog_name;
    unsigned long to_binlog_pos = 0;
    std::string gtid_pos;
    std::string to_gtid_pos;

    bool use_empty_callback = false;
    bool benchmark = false;

    int c;
    while (-1 != (c = ::getopt(argc, argv, "h:u:p:P:d:b:o:B:O:g:G:Cm")))
    {
        switch (c)
        {
        case 'h': host = optarg; break;
        case 'u': user = optarg; break;
        case 'p': password = optarg; break;
        case 'd': database = optarg; break;
        case 'P': port = std::stoi(optarg); break;
        case 'b': binlog_name = optarg; break;
        case 'o': binlog_pos = std::stoul(optarg); break;
        case 'B': to_binlog_name = optarg; break;
        case 'O': to_binlog_pos = std::stoul(optarg); break;
        case 'g': gtid_pos = optarg; break;
        case 'G': to_gtid_pos = optarg; break;
        case 'C': use_empty_callback = true; break;
        case 'm': benchmark = true; break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (host.empty() || user.empty() || database.empty())
    {
        usage(argv[0]);
        return 1;
    }

    std::vector<std::string> tables;

    while (optind < argc) {
        tables.push_back(argv[optind]);
        optind++;
    }

    /////  Real work starts here.

    slave::MasterInfo masterinfo;

    masterinfo.conn_options.mysql_host = host;
    masterinfo.conn_options.mysql_port = port;
    masterinfo.conn_options.mysql_user = user;
    masterinfo.conn_options.mysql_pass = password;
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    bool error = false;

    try {

        std::cout << "Creating client, setting callbacks..." << std::endl;

        slave::DefaultExtState sDefExtState;
        slave::Slave slave(masterinfo, sDefExtState);
        sl = &slave;
        slave::Position pos{binlog_name, binlog_pos};
        pos.parseGtid(gtid_pos);
        slave::Position to_pos{to_binlog_name, to_binlog_pos};
        to_pos.parseGtid(to_gtid_pos);
        sDefExtState.setMasterPosition(pos);

        for (std::vector<std::string>::const_iterator i = tables.begin(); i != tables.end(); ++i) {
            if (use_empty_callback)
                slave.setCallback(database, *i, empty_callback);
            else if (benchmark)
                slave.setCallback(database, *i, bench_callback);
            else
                slave.setCallback(database, *i, callback);
        }

        if (use_empty_callback)
            slave.setXidCallback(empty_xid_callback);
        else if (benchmark)
            slave.setXidCallback(bench_xid_callback);
        else
            slave.setXidCallback(xid_callback);

        std::cout << "Initializing client..." << std::endl;
        slave.init();
        if (!gtid_pos.empty() || !to_gtid_pos.empty())
            slave.enableGtid();

        std::cout << "Reading database structure..." << std::endl;
        slave.createDatabaseStructure();

        try {

            std::cout << "Reading binlogs..." << std::endl;
            if (!to_binlog_name.empty() || 0 != to_binlog_pos || !to_gtid_pos.empty())
            {
                struct timespec start, finish;
                clock_gettime(CLOCK_MONOTONIC_RAW, &start);
                slave.get_remote_binlog([&] ()
                        {
                            const slave::MasterInfo& sMasterInfo = slave.masterInfo();
                            return (isStopping() || sMasterInfo.position.reachedOtherPos(to_pos));
                        });
                clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
                finish.tv_sec  -= start.tv_sec;
                finish.tv_nsec -= start.tv_nsec;
                if (0 > finish.tv_nsec)
                {
                    finish.tv_sec  -= 1;
                    finish.tv_nsec += 1000000000;
                }
                total_work_time = finish.tv_sec + finish.tv_nsec / 1e9;
            }
            else
            {
                slave.get_remote_binlog(isStopping);
            }

        } catch (std::exception& ex) {
            std::cout << "Error in reading binlogs: " << ex.what() << std::endl;
            error = true;
        }

    } catch (std::exception& ex) {
        std::cout << "Error in initializing slave: " << ex.what() << std::endl;
        error = true;
    }

    if (benchmark && !error)
    {
        events_in_time[::time(NULL)] += ev_counter;
        ev_counter = 0;
        commits_in_time[::time(NULL)] += ci_counter;
        ci_counter = 0;

        std::cerr << "\rRead " << total_events << " events" << std::endl << std::endl;
        std::cout << "Total work time: " << total_work_time << " seconds" << std::endl;
        std::cout << "Total read events: " << total_events << std::endl;
        std::cout << "Total read commits: " << total_commits << std::endl;

        std::cout << "\nEvents bench\n";
        for (const auto& x : events_in_time)
            std::cout << x.first << " : " << x.second << std::endl;
        std::cout << "\n";

        std::cout << "Commits bench\n";
        for (const auto& x : commits_in_time)
            std::cout << x.first << " : " << x.second << std::endl;
        std::cout << "\n";
    }

    return 0;
}
