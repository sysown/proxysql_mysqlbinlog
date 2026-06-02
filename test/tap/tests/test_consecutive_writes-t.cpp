/* test_consecutive_writes-t
 *
 * Fire N INSERTs and verify the reader emits N matching lines with
 * strictly consecutive trxids.
 *
 *   1. Reset GTID state.
 *   2. Start reader (streaming mode).
 *   3. Open client, drain ST=.
 *   4. Fire N=100 INSERTs.
 *   5. Read N lines (1st I1, rest I2).
 *   6. Assert:
 *      - count is exactly N
 *      - trxids form a strict +1 sequence
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
	plan(2);

	const int N = 100;

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
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.throughput_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT)");

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty()) {
		BAIL_OUT("failed to start reader");
	}

	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000)) {
		BAIL_OUT("client connect failed");
	}
	BinlogReaderMsg st = client.read_line(5000);
	if (!st.valid() || st.kind != "ST") {
		BAIL_OUT("no ST (error='%s')", st.error.c_str());
	}

	for (int i = 0; i < N; ++i) {
		if (!db.exec("INSERT INTO binlog_reader_test.throughput_t VALUES ()")) {
			BAIL_OUT("INSERT %d failed: %s", i, db.last_error().c_str());
		}
	}

	std::vector<trxid_t> trxids;
	trxids.reserve(N);
	for (int i = 0; i < N; ++i) {
		BinlogReaderMsg m = client.read_line(5000);
		if (!m.valid() || (m.kind != "I1" && m.kind != "I2") ||
		    m.intervals.size() != 1) {
			diag("line %d: kind='%s' error='%s' raw='%s'",
			     i, m.kind.c_str(), m.error.c_str(), m.raw.c_str());
			break;
		}
		trxids.push_back(m.intervals[0].start);
	}

	ok((int)trxids.size() == N,
	   "received %zu/%d lines",
	   trxids.size(), N);

	bool consecutive = !trxids.empty();
	for (size_t i = 1; i < trxids.size(); ++i) {
		if (trxids[i] != trxids[i - 1] + 1) {
			consecutive = false;
			diag("non-consecutive at i=%zu: %lld -> %lld",
			     i, (long long)trxids[i - 1], (long long)trxids[i]);
			break;
		}
	}
	ok(consecutive, "%d trxids are strictly consecutive", N);

	return exit_status();
}
