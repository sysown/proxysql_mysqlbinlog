/* Copyright 2011 ZAO "Begun".
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <algorithm>
#include <regex>
#include <string>

#include "Slave.h"
#include "SlaveStats.h"

#include "Logging.h"

#include "nanomysql.h"

#include <mysql/errmsg.h>
#include <mysql/mysqld_error.h>
#include <mysql/my_global.h>
#include <mysql/m_ctype.h>
#include <mysql/sql_common.h>

#include <signal.h>
#include <unistd.h>

#define packet_end_data 1

#define ER_NET_PACKET_TOO_LARGE 1153
#define ER_MASTER_FATAL_ERROR_READING_BINLOG 1236
#define BIN_LOG_HEADER_SIZE 4


namespace
{
unsigned char *net_store_length_fast(unsigned char *pkg, unsigned int length)
{
    unsigned char *packet=(unsigned char*) pkg;
    if (length < 251)
    {
        *packet=(unsigned char) length;
        return packet+1;
    }
    *packet++=252;
    int2store(packet,(unsigned int) length);
    return packet+2;
}

unsigned char *net_store_data(unsigned char *to, const unsigned char *from, unsigned int length)
{
    to = net_store_length_fast(to,length);
    ::memcpy(to,from,length);
    return to+length;
}

std::string get_hostname()
{
    char buf[256];
    if (::gethostname(buf, 255) == -1)
    {
        LOG_ERROR(log, "Failed to invoke gethostname()");
        return "0.0.0.0";
    }
    return std::string(buf);
}

const char* binlog_checksum_type_names[] =
{
    "NONE",
    "CRC32",
    nullptr
};

unsigned int binlog_checksum_type_length[] =
{
    sizeof("NONE") - 1,
    sizeof("CRC32") - 1,
    0
};

TYPELIB binlog_checksum_typelib =
{
    array_elements(binlog_checksum_type_names) - 1, "",
    binlog_checksum_type_names,
    binlog_checksum_type_length
};

void sigUnblock(int signal)
{
    sigset_t sigSet;
    ::sigemptyset(&sigSet);
    ::sigaddset(&sigSet, signal);
    if (0 != ::pthread_sigmask(SIG_UNBLOCK, &sigSet, nullptr))
        LOG_ERROR(log, "Can't unblock signal: " << errno);
}
}// anonymous-namespace


using namespace slave;


void Slave::init()
{

    LOG_TRACE(log, "Initializing libslave...");

    check_master_version();

    check_master_binlog_format();
    check_master_gtid_mode();

    ext_state.loadMasterPosition(m_master_info.position);

    LOG_TRACE(log, "Libslave initialized OK");
}

void Slave::enableGtid(bool on)
{
    if (on && !m_master_info.gtid_mode)
        throw std::runtime_error("Trying to enable gtid on libslave while gtid_mode is disabled on master");
    m_gtid_enabled = on;
}

void Slave::close_connection()
{
    std::lock_guard<std::mutex> l(m_slave_thread_mutex);
    if (m_slave_thread_id)
    {
        if (::shutdown(mysql.net.fd, SHUT_RDWR) != 0)
            LOG_ERROR(log, "Slave::close_connection: shutdown: failed shutdown socket: " << errno);

        // it is important to send signal after shutdown socket
        // this interrupts all active system calls and unblocks thread
        ::pthread_kill(m_slave_thread_id, SIGURG);
    }
}


void Slave::createDatabaseStructure_(table_order_t& tabs, RelayLogInfo& rli) const
{
    LOG_TRACE(log, "enter: createDatabaseStructure");

    nanomysql::Connection conn(m_master_info.conn_options);
    const collate_map_t collate_map = readCollateMap(conn);


    for (table_order_t::const_iterator it = tabs.begin(); it != tabs.end(); ++ it) {

        LOG_INFO( log, "Creating database structure for: " << it->first << ", Creating table for: " << it->second );
        createTable(rli, it->first, it->second, collate_map, conn);
    }

    LOG_TRACE(log, "exit: createDatabaseStructure");
}



void Slave::createTable(RelayLogInfo& rli,
                        const std::string& db_name, const std::string& tbl_name,
                        const collate_map_t& collate_map, nanomysql::Connection& conn) const
{
    LOG_TRACE(log, "enter: createTable " << db_name << " " << tbl_name);

    nanomysql::Connection::result_t res;

    conn.query("SHOW FULL COLUMNS FROM " + tbl_name + " IN " + db_name);
    conn.store(res);

    std::shared_ptr<Table> table(new Table(db_name, tbl_name));


    LOG_DEBUG(log, "Created new Table object: database:" << db_name << " table: " << tbl_name );

    for (nanomysql::Connection::result_t::const_iterator i = res.begin(); i != res.end(); ++i) {

        //row.at(0) - field name
        //row.at(1) - field type
        //row.at(2) - collation
        //row.at(3) - can be null

        std::map<std::string,nanomysql::field>::const_iterator z = i->find("Field");

        if (z == i->end())
            throw std::runtime_error("Slave::create_table(): DESCRIBE query did not return 'Field'");

        std::string name = z->second.data;

        z = i->find("Type");

        if (z == i->end())
            throw std::runtime_error("Slave::create_table(): DESCRIBE query did not return 'Type'");

        std::string type = z->second.data;

        z = i->find("Null");

        if (z == i->end())
            throw std::runtime_error("Slave::create_table(): DESCRIBE query did not return 'Null'");

        std::string extract_field;

        // Extract field type
        for (size_t tmpi = 0; tmpi < type.size(); ++tmpi) {

            if (!((type[tmpi] >= 'a' && type[tmpi] <= 'z') ||
                  (type[tmpi] >= 'A' && type[tmpi] <= 'Z'))) {

                extract_field = type.substr(0, tmpi);
                break;
            }

            if (tmpi == type.size()-1) {
                extract_field = type;
                break;
            }
        }

        if (extract_field.empty())
            throw std::runtime_error("Slave::create_table(): Regexp error, type not found");

        collate_info ci;
        if ("varchar" == extract_field || "char" == extract_field)
        {
            z = i->find("Collation");
            if (z == i->end())
                throw std::runtime_error("Slave::create_table(): DESCRIBE query did not return 'Collation' for field '" + name + "'");
            const std::string collate = z->second.data;
            collate_map_t::const_iterator it = collate_map.find(collate);
            if (collate_map.end() == it)
                throw std::runtime_error("Slave::create_table(): cannot find collate '" + collate + "' from field "
                                         + name + " type " + type + " in collate info map");
            ci = it->second;
            LOG_DEBUG(log, "Created column: name-type: " << name << " - " << type
                      << " Field type: " << extract_field << " Collation: " << ci.name);
        }
        else
            LOG_DEBUG(log, "Created column: name-type: " << name << " - " << type
                      << " Field type: " << extract_field );

        PtrField field;

        if (extract_field == "int")
            field = PtrField(new Field_long(name, type));

        else if (extract_field == "double")
            field = PtrField(new Field_double(name, type));

        else if (extract_field == "float")
            field = PtrField(new Field_float(name, type));

        else if (extract_field == "timestamp")
            field = PtrField(new Field_timestamp(name, type, m_master_info.is_old_storage));

        else if (extract_field == "datetime")
            field = PtrField(new Field_datetime(name, type, m_master_info.is_old_storage));

        else if (extract_field == "date")
            field = PtrField(new Field_date(name, type));

        else if (extract_field == "year")
            field = PtrField(new Field_year(name, type));

        else if (extract_field == "time")
            field = PtrField(new Field_time(name, type, m_master_info.is_old_storage));

        else if (extract_field == "enum")
            field = PtrField(new Field_enum(name, type));

        else if (extract_field == "set")
            field = PtrField(new Field_set(name, type));

        else if (extract_field == "varchar")
            field = PtrField(new Field_varstring(name, type, ci));

        else if (extract_field == "char")
            field = PtrField(new Field_varstring(name, type, ci));

        else if (extract_field == "tinyint")
            field = PtrField(new Field_tiny(name, type));

        else if (extract_field == "smallint")
            field = PtrField(new Field_short(name, type));

        else if (extract_field == "mediumint")
            field = PtrField(new Field_medium(name, type));

        else if (extract_field == "bigint")
            field = PtrField(new Field_longlong(name, type));

        else if (extract_field == "text")
            field = PtrField(new Field_blob(name, type));

        else if (extract_field == "tinytext")
            field = PtrField(new Field_tinyblob(name, type));

        else if (extract_field == "mediumtext")
            field = PtrField(new Field_mediumblob(name, type));

        else if (extract_field == "longtext")
            field = PtrField(new Field_longblob(name, type));

        else if (extract_field == "blob")
            field = PtrField(new Field_blob(name, type));

        else if (extract_field == "tinyblob")
            field = PtrField(new Field_tinyblob(name, type));

        else if (extract_field == "mediumblob")
            field = PtrField(new Field_mediumblob(name, type));

        else if (extract_field == "longblob")
            field = PtrField(new Field_longblob(name, type));

        else if (extract_field == "decimal")
            field = PtrField(new Field_decimal(name, type));

        else if (extract_field == "bit")
            field = PtrField(new Field_bit(name, type));

        else {
            LOG_ERROR(log, "createTable: class name don't exist: " << extract_field );
            throw std::runtime_error("class name does not exist: " + extract_field);
        }

        table->fields.push_back(field);

    }


    rli.setTable(tbl_name, db_name, table);

}

namespace
{
struct raii_mysql_connector
{
    MYSQL* mysql;
    MasterInfo& m_master_info;
    ExtStateIface &ext_state;
    pthread_t& thread_id;
    std::mutex& mutex;

    raii_mysql_connector(MYSQL* m, MasterInfo& mmi, ExtStateIface& state, pthread_t& tid, std::mutex& mtx)
        : mysql(m)
        , m_master_info(mmi)
        , ext_state(state)
        , thread_id(tid)
        , mutex(mtx)
    {
        connect(false);
    }

    ~raii_mysql_connector() {
        disconnect();
    }

    void connect(bool reconnect) {

        LOG_TRACE(log, "enter: connect_to_master");

        ext_state.setConnecting();

        if (reconnect) {
            disconnect();
        }

        std::lock_guard<std::mutex> l(mutex);
        thread_id = ::pthread_self();

        if (!(mysql_init(mysql))) {

            throw std::runtime_error("Slave::reconnect() : mysql_init() : could not initialize mysql structure");
        }

        bool was_error = reconnect;
        const auto& sConnOptions = m_master_info.conn_options;
        nanomysql::Connection::setOptions(mysql, sConnOptions);

        while (mysql_real_connect(mysql,
                                  sConnOptions.mysql_host.c_str(),
                                  sConnOptions.mysql_user.c_str(),
                                  sConnOptions.mysql_pass.c_str(), 0, sConnOptions.mysql_port, 0, CLIENT_REMEMBER_OPTIONS)
               == 0) {


            ext_state.setConnecting();
            if(!was_error) {
                LOG_ERROR(log, "Couldn't connect to mysql master " << sConnOptions.mysql_host << ":" << sConnOptions.mysql_port);
                was_error = true;
            }

            LOG_TRACE(log, "try connect to master");
            LOG_TRACE(log, "connect_retry = " << m_master_info.connect_retry << ", reconnect = " << reconnect);

            //
            ::sleep(m_master_info.connect_retry);
        }

        if(was_error)
            LOG_INFO(log, "Successfully connected to " << sConnOptions.mysql_host << ":" << m_master_info.mysql_port);


        mysql->reconnect = 1;

        LOG_TRACE(log, "exit: connect_to_master");
    }

    void disconnect()
    {
        std::lock_guard<std::mutex> l(mutex);
        thread_id = 0;
        end_server(mysql);
        mysql_close(mysql);
    }
};
}// anonymous-namespace


void Slave::get_remote_binlog(const std::function<bool()>& _interruptFlag)
{
    // SIGURG is used to unblock read operation on shutdown
    // the default handler for this signal is ignore
    sigUnblock(SIGURG);
    int count_packet = 0;

    generateSlaveId();

    // Moved to Slave member
    // MYSQL mysql;

    raii_mysql_connector __conn(&mysql, m_master_info, ext_state, m_slave_thread_id, m_slave_thread_mutex);

    //connect_to_master(false, &mysql);

    register_slave_on_master(&mysql);

connected:
    do_checksum_handshake(&mysql);

    // Get binlog position saved in ext_state before, or load it
    // from persistent storage. Get false if failed to get binlog position.
    if(!ext_state.getMasterPosition(m_master_info.position))
    {
        // If there is not binlog position saved before,
        // get last binlog name and last binlog position.
        LOG_INFO(log, "There is no saved binlog_pos");
        m_master_info.position = getLastBinlogPos();
        ext_state.setMasterPosition(m_master_info.position);
        ext_state.saveMasterPosition();
    }

    LOG_INFO(log, "Starting from binlog_pos: " << m_master_info.position);

    request_dump(m_master_info.position, &mysql);

    while (!_interruptFlag()) {

        try {

            LOG_TRACE(log, "-- reading event --");

            unsigned long len = read_event(&mysql);

            ext_state.setStateProcessing(true);

            count_packet++;
            LOG_TRACE(log, "Got event with length: " << len << " Packet number: " << count_packet );

            // end of data

            if (len == packet_error || len == packet_end_data) {

                uint mysql_error_number = mysql_errno(&mysql);

                switch(mysql_error_number) {
                    case ER_NET_PACKET_TOO_LARGE:
                        LOG_ERROR(log, "Myslave: Log entry on master is longer than max_allowed_packet on "
                                  "slave. If the entry is correct, restart the server with a higher value of "
                                  "max_allowed_packet. max_allowed_packet=" << mysql_error(&mysql) );
                        break;
                    case ER_MASTER_FATAL_ERROR_READING_BINLOG: // Error -- unknown binlog file.
                        LOG_ERROR(log, "Myslave: fatal error reading binlog. " <<  mysql_error(&mysql) );
                        break;
                    case 2013: // Processing error 'Lost connection to MySQL'
                        LOG_WARNING(log, "Myslave: Error from MySQL: " << mysql_error(&mysql) );
                        // Check if connection closed by user for exiting from the loop
                        if (_interruptFlag())
                        {
                            LOG_INFO(log, "Interrupt flag is true, breaking loop");
                            continue;
                        }
                        break;
                    default:
                        LOG_ERROR(log, "Myslave: Error reading packet from server: " << mysql_error(&mysql)
                                << "; mysql_error: " << mysql_errno(&mysql));
                        break;
                }

                __conn.connect(true);

                goto connected;
            } // len == packet_error

            // Ok event

            if (len == packet_end_data) {
                continue;
            }

            slave::Basic_event_info event;

            if (!slave::read_log_event((const char*) mysql.net.read_pos + 1,
                                       len - 1,
                                       event,
                                       event_stat,
                                       masterGe56(),
                                       m_master_info)) {

                LOG_TRACE(log, "Skipping unknown event.");
                continue;
            }

            //

            LOG_TRACE(log, "Event log position: " << event.log_pos );

            if (event.log_pos != 0) {
                m_master_info.position.log_pos = event.log_pos;
                ext_state.setLastEventTimePos(event.when, event.log_pos);
            }

            LOG_TRACE(log, "seconds_behind_master: " << (::time(NULL) - event.when) );


            // MySQL5.1.23 binlogs can be read only starting from a XID_EVENT
            // MySQL5.1.23 ev->log_pos -- the binlog offset

            if (event.type == XID_EVENT) {

                if (!gtid_next.first.empty())
                    m_master_info.position.addGtid(gtid_next);
                ext_state.setMasterPosition(m_master_info.position);

                LOG_TRACE(log, "Got XID event. Using binlog pos: " << m_master_info.position);

                if (m_xid_callback)
                    m_xid_callback(event.server_id);

            } else if (event.type == QUERY_EVENT) {

                if (!gtid_next.first.empty())
                    m_master_info.position.addGtid(gtid_next);
                ext_state.setMasterPosition(m_master_info.position);

                LOG_TRACE(log, "Got XID event. Using binlog pos: " << m_master_info.position);

                if (m_xid_callback)
                    m_xid_callback(event.server_id);

            } else  if (event.type == ROTATE_EVENT) {

                slave::Rotate_event_info rei(event.buf, event.event_len);

                /*
                 * new_log_ident - new binlog name
                 * pos - position of the starting event
                 */

                LOG_INFO(log, "Got rotate event.");

                /* WTF
                 */

                if (event.when == 0) {

                    //LOG_TRACE(log, "ROTATE_FAKE");
                }

                m_master_info.position.log_name = rei.new_log_ident;
                m_master_info.position.log_pos = rei.pos; // this will always be equal to 4

                ext_state.setMasterPosition(m_master_info.position);

                LOG_TRACE(log, "new position is " << m_master_info.position);
                LOG_TRACE(log, "ROTATE_EVENT processed OK.");
            }
            else if (event.type == GTID_LOG_EVENT)
            {
                LOG_TRACE(log, "Got GTID event.");
                if (!gtid_next.first.empty())
                {
                    m_master_info.position.addGtid(gtid_next);
                    ext_state.setMasterPosition(m_master_info.position);
                }
                Gtid_event_info gei(event.buf, event.event_len);
                LOG_TRACE(log, "GTID_NEXT: sid = " << gei.m_sid << ", gno =  " << gei.m_gno);
                gtid_next.first = gei.m_sid;
                gtid_next.second = gei.m_gno;
            }

            if (process_event(event, m_rli))
            {
                LOG_TRACE(log, "Error in processing event.");
            }



        } catch (const std::exception& _ex ) {

            LOG_ERROR(log, "Met exception in get_remote_binlog cycle. Message: " << _ex.what() );
            if (event_stat)
                event_stat->tickError();
            usleep(1000*1000);
            continue;

        }

    } //while

    LOG_WARNING(log, "Binlog monitor was stopped. Binlog events are not listened.");

    deregister_slave_on_master(&mysql);
}

void Slave::register_slave_on_master(MYSQL* mysql)
{
    uchar buf[1024], *pos= buf;

    unsigned int report_user_len=0, report_password_len=0;

    const std::string report_host = get_hostname();

    const char* report_user = "begun_slave";
    const char* report_password = "begun_slave";
    unsigned int report_port = 0;
    unsigned long rpl_recovery_rank = 0;

    report_user_len= strlen(report_user);
    report_password_len= strlen(report_password);

    LOG_DEBUG(log, "Registering slave on master: m_server_id = " << m_server_id << "...");

    int4store(pos, m_server_id);
    pos+= 4;
    pos= net_store_data(pos, (uchar*)report_host.c_str(), report_host.size());
    pos= net_store_data(pos, (uchar*)report_user, report_user_len);
    pos= net_store_data(pos, (uchar*)report_password, report_password_len);
    int2store(pos, (unsigned short) report_port);
    pos+= 2;
    int4store(pos, rpl_recovery_rank);
    pos+= 4;

    /* The master will fill in master_id */
    int4store(pos, 0);
    pos+= 4;

    if (simple_command(mysql, COM_REGISTER_SLAVE, buf, (size_t) (pos-buf), 0)) {

        LOG_ERROR(log, "Unable to register slave.");
        throw std::runtime_error("Slave::register_slave_on_master(): Error registring on slave: " +
                                 std::string(mysql_error(mysql)));
    }

    LOG_TRACE(log, "Success registering slave on master");
}

void Slave::deregister_slave_on_master(MYSQL* mysql)
{
    LOG_DEBUG(log, "Deregistering slave on master: m_server_id = " << m_server_id << "...");
    // Last '1' means 'no checking', otherwise command can hung
    simple_command(mysql, COM_QUIT, 0, 0, 1);
}

void Slave::check_master_version()
{
    nanomysql::Connection conn(m_master_info.conn_options);
    nanomysql::Connection::result_t res;

    conn.query("SELECT VERSION()");
    conn.store(res);

    if (res.size() == 1 && res[0].size() == 1)
    {
        std::string tmp = res[0].begin()->second.data;
        int major, minor, patch;
        if (3 == sscanf(tmp.c_str(), "%d.%d.%d", &major, &minor, &patch))
        {
            m_master_version = major * 10000 + minor * 100 + patch;
            // since 5.6.4 storage for temporal types has changed
            m_master_info.is_old_storage = m_master_version < 50604;
            static const int min_version = 50123;   // 5.1.23
            if (m_master_version >= min_version)
                return;
        }
        throw std::runtime_error("Slave::check_master_version(): got invalid version: " + tmp);
    }

    throw std::runtime_error("Slave::check_master_version(): could not SELECT VERSION()");
}

void Slave::check_master_binlog_format()
{
    nanomysql::Connection conn(m_master_info.conn_options);
    nanomysql::Connection::result_t res;

    conn.query("SHOW GLOBAL VARIABLES LIKE 'binlog_format'");
    conn.store(res);

    if (res.size() == 1 && res[0].size() == 2) {

        std::map<std::string,nanomysql::field>::const_iterator z = res[0].find("Value");

        if (z == res[0].end())
            throw std::runtime_error("Slave::create_table(): SHOW GLOBAL VARIABLES query did not return 'Value'");

        std::string tmp = z->second.data;

        if (tmp == "ROW") {
            return;

        } else {
            throw std::runtime_error("Slave::check_binlog_format(): got invalid binlog format: " + tmp);
        }
    }


    throw std::runtime_error("Slave::check_binlog_format(): Could not SHOW GLOBAL VARIABLES LIKE 'binlog_format'");
}

void Slave::check_master_gtid_mode()
{
    nanomysql::Connection conn(m_master_info.conn_options);
    nanomysql::Connection::result_t res;

    conn.query("SHOW GLOBAL VARIABLES LIKE 'gtid_mode'");
    conn.store(res);

    m_master_info.gtid_mode = false;
    if (res.size() == 1 && res[0].size() == 2)
    {
        auto it = res[0].find("Value");
        if (it == res[0].end())
            throw std::runtime_error("Slave::check_master_gtid_mode(): SHOW GLOBAL VARIABLES query did not return 'Value'");

        m_master_info.gtid_mode = (it->second.data == "ON");
    }
}

void Slave::do_checksum_handshake(MYSQL* mysql)
{
    const char query[] = "SET @master_binlog_checksum= @@global.binlog_checksum";

    if (mysql_real_query(mysql, query, static_cast<ulong>(strlen(query))))
    {
        if (mysql_errno(mysql) != ER_UNKNOWN_SYSTEM_VARIABLE)
        {
            mysql_free_result(mysql_store_result(mysql));
            throw std::runtime_error("Slave::do_checksum_handshake(MYSQL* mysql): query 'SET @master_binlog_checksum= @@global.binlog_checksum' failed");
        }
        mysql_free_result(mysql_store_result(mysql));
    }
    else
    {
        mysql_free_result(mysql_store_result(mysql));
        MYSQL_RES* master_res = nullptr;
        MYSQL_ROW master_row = nullptr;
        const char select_query[] = "SELECT @master_binlog_checksum";

        if (!mysql_real_query(mysql, select_query, static_cast<ulong>(strlen(select_query))) &&
            (master_res = mysql_store_result(mysql)) &&
            (master_row = mysql_fetch_row(master_res)) &&
            (master_row[0] != NULL))
        {
            m_master_info.checksum_alg = static_cast<enum_binlog_checksum_alg>(find_type(master_row[0], &binlog_checksum_typelib, 1) - 1);
        }

        if (master_res)
            mysql_free_result(master_res);

        if (m_master_info.checksum_alg != BINLOG_CHECKSUM_ALG_OFF && m_master_info.checksum_alg != BINLOG_CHECKSUM_ALG_CRC32)
            throw std::runtime_error("Slave::do_checksum_handshake(MYSQL* mysql): unknown checksum algorithm");
    }

    LOG_TRACE(log, "Success doing checksum handshake");
}



namespace
{
std::string checkAlterOrCreateQuery(const std::string& str)
{
/*
    static const std::regex query_regex(R"(\s*(?:alter\s+table|create\s+table(?:\s+if\s+not\s+exists)?)\s+(?:`?\w+`?\.)?`?(\w+)`?(?:[^\w\.`].*$|$))",
                                        std::regex_constants::optimize | std::regex_constants::icase);

    std::string s;
    std::replace_copy(str.begin(), str.end(), std::back_inserter(s), '\n', ' ');

    std::smatch sm;
    if (std::regex_match(s, sm, query_regex))
        return sm[1];
*/
    return "";
}
}// anonymouos-namespace



int Slave::process_event(const slave::Basic_event_info& bei, RelayLogInfo& m_rli)
{


    if (bei.when < 0 &&
        bei.type != FORMAT_DESCRIPTION_EVENT)
        return 0;

    switch (bei.type) {

    case QUERY_EVENT:
    {
        // Check for ALTER TABLE or CREATE TABLE

        slave::Query_event_info qei(bei.buf, bei.event_len);

        LOG_TRACE(log, "Received QUERY_EVENT: " << qei.query);

        const auto tbl_name = checkAlterOrCreateQuery(qei.query);
        if (!tbl_name.empty())
        {
            const auto key = std::make_pair(qei.db_name, tbl_name);
            if (m_table_order.count(key) == 1)
            {
                LOG_DEBUG(log, "Rebuilding database structure.");
                table_order_t order {key};
                createDatabaseStructure_(order, m_rli);
                auto it = m_rli.m_table_map.find(key);
                if (it != m_rli.m_table_map.end())
                {
                    it->second->m_callback = m_callbacks[key];
                    it->second->m_filter = m_filters[key];
                }
            }
        }
        break;
    }

    case TABLE_MAP_EVENT:
    {
        LOG_TRACE(log, "Got TABLE_MAP_EVENT.");

        slave::Table_map_event_info tmi(bei.buf, bei.event_len);

        m_rli.setTableName(tmi.m_table_id, tmi.m_tblnam, tmi.m_dbnam);

        if (m_master_version >= 50604)
        {
            auto table = m_rli.getTable(std::make_pair(tmi.m_dbnam, tmi.m_tblnam));
            if (table && tmi.m_cols_types.size() == table->fields.size())
            {
                int i = 0;
                for (const auto& x : tmi.m_cols_types)
                {
                    switch (x)
                    {
                    case MYSQL_TYPE_TIMESTAMP:
                    case MYSQL_TYPE_DATETIME:
                    case MYSQL_TYPE_TIME:
                        static_cast<Field_temporal*>(table->fields[i].get())->reset(true);
                        break;
                    case MYSQL_TYPE_TIMESTAMP2:
                    case MYSQL_TYPE_DATETIME2:
                    case MYSQL_TYPE_TIME2:
                        static_cast<Field_temporal*>(table->fields[i].get())->reset(false);
                        break;
                    default:
                        break;
                    }
                    i++;
                }
            }
        }

        if (event_stat)
            event_stat->processTableMap(tmi.m_table_id, tmi.m_tblnam, tmi.m_dbnam);

        break;
    }

    case WRITE_ROWS_EVENT_V1:
    case UPDATE_ROWS_EVENT_V1:
    case DELETE_ROWS_EVENT_V1:
    case WRITE_ROWS_EVENT:
    case UPDATE_ROWS_EVENT:
    case DELETE_ROWS_EVENT:
    {
        LOG_TRACE(log, "Got " << (bei.type == WRITE_ROWS_EVENT_V1 || bei.type == WRITE_ROWS_EVENT ? "WRITE" :
                                  bei.type == DELETE_ROWS_EVENT_V1 || bei.type == DELETE_ROWS_EVENT ? "DELETE" :
                                  "UPDATE") << "_ROWS_EVENT");

        Row_event_info roi(bei.buf, bei.event_len, (bei.type == UPDATE_ROWS_EVENT_V1 || bei.type == UPDATE_ROWS_EVENT), masterGe56());

        apply_row_event(m_rli, bei, roi, ext_state, event_stat);

        break;
    }

    default:
        break;
    }

    return 0;
}

void Slave::request_dump_wo_gtid(const std::string& logname, unsigned long start_position, MYSQL* mysql)
{
    uchar buf[128];

    /*
    COM_BINLOG_DUMP accepts only 4 bytes for the position, so we are forced to
    cast to uint32.
    */

    //
    //start_position = 4;

    int binlog_flags = 0;
    int4store(buf, (uint32)start_position);
    int2store(buf + BIN_LOG_HEADER_SIZE, binlog_flags);

    uint logname_len = logname.size();
    int4store(buf + 6, m_server_id);

    memcpy(buf + 10, logname.data(), logname_len);

    if (simple_command(mysql, COM_BINLOG_DUMP, buf, logname_len + 10, 1)) {

        LOG_ERROR(log, "Error sending COM_BINLOG_DUMP");
        throw std::runtime_error("Error in sending COM_BINLOG_DUMP");
    }
}

void Slave::request_dump(const Position& pos, MYSQL* mysql)
{
    if (!m_gtid_enabled)
    {
        request_dump_wo_gtid(pos.log_name, pos.log_pos, mysql);
    }
    else
    {
#if MYSQL_VERSION_ID >= 50605
        size_t encoded_size = m_master_info.position.encodedGtidSize();
        uchar* buf = (uchar*)malloc(encoded_size + 22);

        int2store(buf, 4);
        int4store(buf + 2, m_server_id);
        int4store(buf + 6, 0);
        int8store(buf + 10, 4LL);

        int4store(buf + 18, encoded_size);
        m_master_info.position.encodeGtid(buf + 22);

        if (simple_command(mysql, COM_BINLOG_DUMP_GTID, buf, encoded_size + 22, 1))
        {
            LOG_ERROR(log, "Error sending COM_BINLOG_DUMP_GTID");
            free(buf);
            throw std::runtime_error("Error in sending COM_BINLOG_DUMP_GTID");
        }
        free(buf);
#else
        LOG_ERROR(log, "libmysqlclient >= 5.6.5 needed to use GTID replication");
        throw std::runtime_error("libmysqlclient >= 5.6.5 needed to use GTID replication");
#endif
    }
}

ulong Slave::read_event(MYSQL* mysql)
{

    ulong len;
    ext_state.setStateProcessing(false);

#if MYSQL_VERSION_ID < 50705
    len = cli_safe_read(mysql);
#else
    len = cli_safe_read(mysql, nullptr);
#endif

    if (len == packet_error) {
        LOG_ERROR(log, "Myslave: Error reading packet from server: " << mysql_error(mysql)
                  << "; mysql_error: " << mysql_errno(mysql));

        return packet_error;
    }

    // check for end-of-data
    if (len < 8 && mysql->net.read_pos[0] == 254) {

        LOG_ERROR(log, "read_event(): end of data\n");
        return packet_end_data;
    }

    return len;
}

void Slave::generateSlaveId()
{

    std::set<unsigned int> server_ids;

    nanomysql::Connection conn(m_master_info.conn_options);
    nanomysql::Connection::result_t res;

    conn.query("SHOW SLAVE HOSTS");
    conn.store(res);

    for (nanomysql::Connection::result_t::const_iterator i = res.begin(); i != res.end(); ++i) {

        //row[0] - server_id

        std::map<std::string,nanomysql::field>::const_iterator z = i->find("Server_id");

        if (z == i->end())
            throw std::runtime_error("Slave::create_table(): SHOW SLAVE HOSTS query did not return 'Server_id'");

        server_ids.insert(::strtoul(z->second.data.c_str(), NULL, 10));
    }

    unsigned int serveroid = ::time(NULL);
    serveroid ^= (::getpid() << 16);

    while (1) {

        if (server_ids.count(serveroid) != 0) {
            serveroid++;
        } else {
            break;
        }
    }

    m_server_id = serveroid;

    LOG_DEBUG(log, "Generated m_server_id = " << m_server_id);
}

Position Slave::getLastBinlogPos() const
{
    nanomysql::Connection conn(m_master_info.conn_options);
    nanomysql::Connection::result_t res;

    static const std::string query = "SHOW MASTER STATUS";
    conn.query(query);
    conn.store(res);

    if (res.size() == 1) {
        Position result;

        std::map<std::string,nanomysql::field>::const_iterator z = res[0].find("File");

        if (z == res[0].end())
            throw std::runtime_error("Slave::create_table(): " + query + " query did not return 'File'");

        result.log_name = z->second.data;

        z = res[0].find("Position");

        if (z == res[0].end())
            throw std::runtime_error("Slave::create_table(): " + query + " query did not return 'Position'");

        result.log_pos = std::strtoul(z->second.data.c_str(), nullptr, 10);

        z = res[0].find("Executed_Gtid_Set");
        if (z != res[0].end())
            result.parseGtid(z->second.data);

        return result;
    }

    throw std::runtime_error("Slave::getLastBinLog(): Could not " + query);
}
