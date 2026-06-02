/* test_connect_disconnect-t
 *
 * Exercise sequential client connect/disconnect cycles against a
 * long-running reader. The reader must handle N cycles cleanly (no fd
 * leak, no crash, fresh ST= to each new client) and the final
 * cumulative state must reflect all the INSERTs done during the loop.
 *
 *   1. Reset GTID state.
 *   2. Start reader (streaming mode).
 *   3. Loop N=100:
 *      - Open client; read ST=.
 *      - INSERT one row.
 *      - Read the resulting I1; capture its trxid.
 *      - Disconnect (client goes out of scope).
 *   4. Open one final client; read ST=.
 *   5. Assert the final ST covers the last loop's trxid.
 */

#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "proxysql_gtid.h"
#include "tap.h"
#include "tap_utils.h"

int main() {
	plan(3);

	CommandLine cli;
	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli)) {
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	}

	if (!cli.reader_bin.empty())  // reset gtid only in spawn mode
		db.reset_gtid_set();

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.connect_disconnect_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT)");

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty()) {
		BAIL_OUT("failed to start reader");
	}

	const int N = 100;
	int cycles_ok = 0;
	trxid_t last_trxid = 0;

	for (int i = 0; i < N; ++i) {
		BinlogReaderClient client;
		if (!client.connect(reader_host, cli.reader_port, 2000)) {
			diag("cycle %d: connect failed", i);
			continue;
		}
		BinlogReaderMsg st = client.read_line(5000);
		if (!st.valid() || st.kind != "ST") {
			diag("cycle %d: no ST (error='%s' raw='%s')",
			     i, st.error.c_str(), st.raw.c_str());
			continue;
		}
		if (!db.exec("INSERT INTO binlog_reader_test.connect_disconnect_t VALUES ()")) {
			BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
		}
		BinlogReaderMsg m = client.read_line(5000);
		if (!m.valid() || m.kind != "I1" || m.intervals.size() != 1) {
			diag("cycle %d: expected I1, got kind='%s' raw='%s'",
			     i, m.kind.c_str(), m.raw.c_str());
			continue;
		}
		last_trxid = m.intervals[0].start;
		cycles_ok++;
	}
	ok(cycles_ok == N,
	   "all %d connect/insert/I1/disconnect cycles succeeded (got %d/%d)",
	   N, cycles_ok, N);

	// Final client: reader is still serving and the ST reflects the
	// cumulative state.
	BinlogReaderClient final_client;
	if (!final_client.connect(reader_host, cli.reader_port, 2000)) {
		BAIL_OUT("final connect failed — reader may have crashed");
	}
	BinlogReaderMsg final_st = final_client.read_line(5000);
	ok(final_st.valid() && final_st.kind == "ST" && !final_st.intervals.empty(),
	   "final ST received (intervals=%zu, raw='%s')",
	   final_st.intervals.size(), final_st.raw.c_str());

	trxid_t final_max = 0;
	for (auto& iv : final_st.intervals) {
		if (iv.end > final_max) final_max = iv.end;
	}
	ok(final_max >= last_trxid,
	   "final ST covers all loop trxids (final max=%lld, last loop trxid=%lld)",
	   (long long)final_max, (long long)last_trxid);

	return exit_status();
}
