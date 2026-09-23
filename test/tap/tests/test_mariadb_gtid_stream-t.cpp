/* mariadb_gtid_stream-t
 *
 * Live MariaDB snapshot and stream. Skip unless VERSION() contains MariaDB.
 * CREATE happens before setup_reader so @@gtid_binlog_pos is non-empty.
 */

#include <mysql.h>

#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"

int main() {
	plan(3);

	CommandLine cli;
	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect: %s", db.last_error().c_str());
	if (mysql_query(db.raw(), "SELECT VERSION()"))
		BAIL_OUT("SELECT VERSION failed: %s", mysql_error(db.raw()));
	MYSQL_RES* ver_res = mysql_store_result(db.raw());
	MYSQL_ROW ver_row = ver_res ? mysql_fetch_row(ver_res) : nullptr;
	std::string ver = (ver_row && ver_row[0]) ? ver_row[0] : "";
	mysql_free_result(ver_res);
	if (ver.find("MariaDB") == std::string::npos) {
		skip(3, "not MariaDB (version=%s)", ver.c_str());
		return exit_status();
	}

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.mdb_gtid_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)");

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty())
		BAIL_OUT("failed to start reader");
	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000))
		BAIL_OUT("cannot connect to reader");

	BinlogReaderMsg st = client.read_line(10000);
	ok(st.valid() && st.kind == "ST" && st.uuid == "0" && !st.intervals.empty()
	       && st.intervals[0].start == 1,
	   "MariaDB ST= is domain 0 watermark (raw='%s')", st.raw.c_str());

	if (!db.exec("INSERT INTO binlog_reader_test.mdb_gtid_t (v) VALUES (1)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	BinlogReaderMsg m1 = client.read_line(5000);
	ok(m1.valid() && m1.kind == "I1" && m1.uuid == "0" &&
	       m1.intervals.size() == 1 && m1.intervals[0].start > 0,
	   "INSERT emits I1=0:<seq> (raw='%s')", m1.raw.c_str());

	if (!db.exec("INSERT INTO binlog_reader_test.mdb_gtid_t (v) VALUES (2)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	BinlogReaderMsg m2 = client.read_line(5000);
	ok(m2.valid() && m2.kind == "I2" &&
	       m2.intervals.size() == 1 &&
	       m2.intervals[0].start == m1.intervals[0].start + 1,
	   "second INSERT emits I2=<seq+1> (raw='%s')", m2.raw.c_str());

	return exit_status();
}
