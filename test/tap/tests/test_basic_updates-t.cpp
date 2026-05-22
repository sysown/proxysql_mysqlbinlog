/* updates-t
 *
 *   1. Ensure target MySQL has a known test table (DDL).
 *   2. Start reader; read ST= and capture the baseline trxid.
 *   3. INSERT once → next line must be I1=<uuid>:<trxid> (first
 *      update for the client's uuid).
 *   4. INSERT again → next line must be I2=<trxid+1> (same uuid
 *      implied; reader switches to I2 once uuid is known).
 *   5. INSERT a third time → must stay I2=<trxid+2>; the reader must
 *      not regress to I1 when the uuid hasn't changed.
 */

#include <string>
#include <vector>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "proxysql_gtid.h"
#include "tap.h"
#include "tap_utils.h"

int main() {
	plan(4);

	CommandLine cli;
	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli)) {
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	}

	db.reset_gtid_set();

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.updates_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)");

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty()) {
		BAIL_OUT("failed to start reader");
	}

	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000)) {
		BAIL_OUT("cannot connect to reader at %s:%d", reader_host.c_str(),
		         cli.reader_port);
	}

	BinlogReaderMsg st = client.read_line(10000);
	ok(st.valid() && st.kind == "ST" && !st.uuid.empty() && !st.intervals.empty(),
	   "ST= received (uuid='%s', intervals=%zu, raw='%s')",
	   st.uuid.c_str(), st.intervals.size(), st.raw.c_str());
	if (!st.valid() || st.kind != "ST") {
		return exit_status();
	}

	const std::string expected_uuid = strip_dashes(st.uuid);

	if (!db.exec("INSERT INTO binlog_reader_test.updates_t (v) VALUES (100)")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}
	BinlogReaderMsg m1 = client.read_line(5000);
	const trxid_t got1 = m1.intervals.empty() ? 0 : m1.intervals[0].start;
	ok(m1.valid() && m1.kind == "I1" && m1.uuid == expected_uuid &&
	       m1.intervals.size() == 1 && got1 > 0,
	   "next is I1=%s:%lld (uuid match, trxid > 0; raw='%s')",
	   m1.uuid.c_str(), (long long)got1, m1.raw.c_str());

	if (!db.exec("INSERT INTO binlog_reader_test.updates_t (v) VALUES (200)")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}
	BinlogReaderMsg m2 = client.read_line(5000);
	const trxid_t got2 = m2.intervals.empty() ? 0 : m2.intervals[0].start;
	ok(m2.valid() && m2.kind == "I2" &&
	       m2.intervals.size() == 1 && got2 == got1 + 1,
	   "next is I2=%lld (expected I1's trxid + 1 = %lld, raw='%s')",
	   (long long)got2, (long long)(got1 + 1), m2.raw.c_str());

	if (!db.exec("INSERT INTO binlog_reader_test.updates_t (v) VALUES (300)")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}
	BinlogReaderMsg m3 = client.read_line(5000);
	const trxid_t got3 = m3.intervals.empty() ? 0 : m3.intervals[0].start;
	ok(m3.valid() && m3.kind == "I2" &&
	       m3.intervals.size() == 1 && got3 == got2 + 1,
	   "next is still I2=%lld (expected I2's trxid + 1 = %lld, raw='%s')",
	   (long long)got3, (long long)(got2 + 1), m3.raw.c_str());

	return exit_status();
}
