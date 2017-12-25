#include <getopt.h>
#include <iostream>
#include "nanomysql.h"
#include "Slave.h"

// Utility for libslave benchmarks:
// 1. Creates empty table.
// 2. Outputs current binlog position.
// 3. Fills table with data.
// 4. Outputs current binlog position.
// 5. Updates table.
// 6. Outputs current binlog position.

void usage(const char* name)
{
    std::cout << "Usage: " << name << " -h <mysql host> -u <mysql user> -p <mysql password> -d <mysql database> -P <mysql port>"
              << " -s <short_table> -l <long_table> -b <bulk size> -c <bulk count>" << std::endl;
}

void createTable(const nanomysql::mysql_conn_opts& opts, const std::string& aTableName,
                 unsigned bulk_size, unsigned bulk_count,
                 const std::string& aTableDesc,
                 const std::string& aQueryStart, const std::string& aQueryPart,
                 const std::string& aUpdateQuery
                 )
{
    std::cerr << ::time(NULL) << " Recreating table " << aTableName << "..." << std::endl;
    nanomysql::Connection conn(opts);
    conn.query("DROP TABLE IF EXISTS " + aTableName);
    conn.query(aTableDesc);

    std::cerr << ::time(NULL) << " Getting slave current position..." << std::endl;

    slave::MasterInfo sMasterInfo(opts, 10);
    slave::Slave sSlave(sMasterInfo);

    slave::Position sBinlogPos = sSlave.getLastBinlogPos();
    std::cout << "Master pos before insert is " << sBinlogPos << std::endl;

    std::string sQuery = aQueryStart;

    for (size_t i = bulk_size; i > 1; --i)
        sQuery += aQueryPart + ",";
    sQuery += aQueryPart;

    timespec start, stop;
    long long diff;

    std::cerr << ::time(NULL) << " Inserting data..." << std::endl;
    time_t sLastOutputTime = 0;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    for (unsigned i = bulk_count; i > 0; --i)
    {
        const time_t ct = ::time(nullptr);
        if (ct != sLastOutputTime || 1 == i)
        {
            std::cerr << "\rInserting bulk " << (bulk_count - i + 1) << " from " << bulk_count;
            sLastOutputTime = ct;
        }
        conn.query(sQuery);
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
    std::cerr << std::endl;

    diff = (stop.tv_sec - start.tv_sec) * 1000000000 + (stop.tv_nsec - start.tv_nsec);
    std::cout << "Inserted in " << (diff / 1e9) << " seconds\n";

    sBinlogPos = sSlave.getLastBinlogPos();
    std::cout << "Master pos after insert is " << sBinlogPos << std::endl;

    std::cerr << ::time(NULL) << " Updating data..." << std::endl;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    conn.query(aUpdateQuery);
    clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
    diff = (stop.tv_sec - start.tv_sec) * 1000000000 + (stop.tv_nsec - start.tv_nsec);
    std::cout << "Updated in " << (diff / 1e9) << " seconds\n";

    sBinlogPos = sSlave.getLastBinlogPos();
    std::cout << "Master pos after update is " << sBinlogPos << std::endl;
}

int main(int argc, char** argv)
{
    nanomysql::mysql_conn_opts opts;
    std::string short_table;
    std::string long_table;
    unsigned int bulk_size = 0;
    unsigned int bulk_count = 0;

    int c;
    while (-1 != (c = ::getopt(argc, argv, "h:u:p:P:s:l:d:b:c:")))
    {
        switch (c)
        {
        case 'h': opts.mysql_host = optarg; break;
        case 'u': opts.mysql_user = optarg; break;
        case 'p': opts.mysql_pass= optarg; break;
        case 'd': opts.mysql_db = optarg; break;
        case 'P': opts.mysql_port = std::stoi(optarg); break;
        case 's': short_table = optarg; break;
        case 'l': long_table = optarg; break;
        case 'b': bulk_size = std::stoi(optarg); break;
        case 'c': bulk_count = std::stoi(optarg); break;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (opts.mysql_host.empty() || opts.mysql_user.empty() || opts.mysql_db.empty() ||
        (short_table.empty() && long_table.empty()) || 0 == bulk_size || 0 == bulk_count)
    {
        usage(argv[0]);
        return 1;
    }

    if (!short_table.empty())
        createTable(opts, short_table, bulk_size, bulk_count,
               "CREATE TABLE " + short_table + "(\n"
               "id int NOT NULL auto_increment,\n"
               "user_id int NOT NULL,\n"
               "banner_template_id int NOT NULL,\n"
               "PRIMARY KEY (id)\n"
               ")",

               "INSERT INTO " + short_table + "(user_id, banner_template_id) values",
               "(1, 2)",
               "UPDATE " + short_table + " SET user_id = 12"
            );

    if (!long_table.empty())
        createTable(opts, long_table, bulk_size, bulk_count, "CREATE TABLE " + long_table + "(\n"
               "id int NOT NULL auto_increment,\n"
               "created timestamp NOT NULL DEFAULT 0,\n"
               "updated timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,\n"
               "status enum('active','blocked','deleted') NOT NULL DEFAULT 'active',\n"
               "system_status enum('active','blocked','deleted') NOT NULL DEFAULT 'active',\n"

               "alias varchar(255) NOT NULL,\n"
               "user_id int NOT NULL,\n"
               "banner_template_id int NOT NULL,\n"
               "type_id text NOT NULL,\n"
               "flags set('default_dithering', 'console', 'ext_console', 'int_console', 'abstract', 'force_use_usergeo', 'filter_topics', 'filter_thematics', 'rtb', 'own_ctr', 'interface_only') NOT NULL DEFAULT 'default_dithering,own_ctr',\n"
               "banners_count int unsigned NOT NULL DEFAULT 2,\n"
               "shows_threshold_coeff double not null default 1.0,\n"
               "min_a_ratio double not null default 0.0,\n"
               "cpm_limit int NOT NULL,\n"
               "block_cpm_limit int NOT NULL,\n"
               "a_block_cpm_limit int NOT NULL,\n"
               "block_banners text NOT NULL,\n"

               "borrow_ctr_pad_id varchar(255) NOT NULL,\n"
               "borrow_ctr_coeff varchar(255) NOT NULL,\n"
               "borrow_ctr_no_ctr_action enum('no_show', 'default', 'default_with_coeff') NOT NULL default 'default',\n"
               "banner_units int NOT NULL default 10,\n"

               "marketing_min_share double NOT NULL,\n"
               "cpm_limit_share double NOT NULL default 1.0,\n"
               "thematics_similarity_threshold double NOT NULL default 1.0,\n"

               "uniq_shows_limit_coeff double NOT NULL default 1.0,\n"

               "settings text not null default \"\",\n"

               "PRIMARY KEY (id)\n"
               ")",

               "INSERT INTO " + long_table + "(alias, settings) values",
               "(\"alias_alias\", \"{no settings}\")",
               "UPDATE " + long_table + " SET user_id = 1"
            );

    return 0;
}
