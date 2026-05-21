/* binlog_reader_startup-t
 *
 *   1. ensure target MySQL has a non-empty GTID_EXECUTED (issue a write)
 *   2. obtain a running reader — either spawn one (reader_bin set) or
 *      connect to a pre-existing one at reader_host:reader_port
 *   3. wait for the listener port to accept (spawn mode only)
 *   4. connect a client and verify the first line is ST=<gtid-set>
 */

#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"

int main() {
	plan(2);

	CommandLine cli;
	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli)) {
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	}
	diag("connected to MySQL");

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.startup_t "
	        "(id INT PRIMARY KEY)");
	db.exec("INSERT IGNORE INTO binlog_reader_test.startup_t VALUES (1)");
	std::string gtid = db.gtid_executed();
	ok(!gtid.empty(), "MySQL gtid_executed is non-empty (%s)", gtid.c_str());

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty()) {
		BAIL_OUT("failed to start %s", cli.reader_bin.c_str());
	}

	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000)) {
		BAIL_OUT("cannot connect to reader at %s:%d", reader_host.c_str(),
		         cli.reader_port);
	}

	BinlogReaderMsg msg = client.read_line(10000);
	if (!msg.valid()) {
		diag("read_line: line='%s', error=%s, errno=%d",
		     msg.raw.c_str(), msg.error.c_str(), msg.last_errno);
	}
	ok(msg.valid() && msg.kind == "ST" && msg.raw == "ST=" + gtid,
	   "first line is ST=%s (got kind='%s', raw='%s')",
	   gtid.c_str(), msg.kind.c_str(), msg.raw.c_str());

	return exit_status();
}
