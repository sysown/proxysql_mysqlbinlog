/* test_batched_updates-t
 *
 * With the reader in batched mode (-b 1 -t N), multiple INSERTs that
 * land inside one timer window must be reported as a single I3 (or I4)
 * line whose interval covers the whole batch.
 *
 *   1. Reset GTID state so trxids are predictable.
 *   2. Start reader with batching=1 and freq_ms=300.
 *   3. Read ST=, capture baseline.
 *   4. Fire 5 INSERTs back-to-back.
 *   5. Read next line — must be I3=<uuid>:<baseline+1>-<baseline+5>.
 *   6. Fire 5 more INSERTs.
 *   7. Read next line — must be I4=<baseline+6>-<baseline+10>
 *      (same uuid implied).
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

static trxid_t max_interval_end(const std::vector<TrxId_Interval>& ivs) {
	trxid_t mx = 0;
	for (auto& iv : ivs) {
		if (iv.end > mx) mx = iv.end;
	}
	return mx;
}

int main() {
	plan(3);

	CommandLine cli;
	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli)) {
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	}

	db.reset_gtid_set();

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.batching_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)");

	// Batched mode: -b 1 -t 300 (300 ms window).
	cli.batching = 1;
	cli.freq_ms = 300;
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
	if (!st.valid()) return exit_status();

	const std::string expected_uuid = strip_dashes(st.uuid);
	const trxid_t base = max_interval_end(st.intervals);

	// Fire 5 INSERTs within the 300 ms window.
	for (int i = 1; i <= 5; ++i) {
		if (!db.exec("INSERT INTO binlog_reader_test.batching_t (v) VALUES (" +
		             std::to_string(i) + ")")) {
			BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
		}
	}

	BinlogReaderMsg b1 = client.read_line(2000);
	const trxid_t b1_start = b1.intervals.empty() ? 0 : b1.intervals[0].start;
	const trxid_t b1_end   = b1.intervals.empty() ? 0 : b1.intervals[0].end;
	ok(b1.valid() && b1.kind == "I3" && b1.uuid == expected_uuid &&
	       b1.intervals.size() == 1 &&
	       b1_start == base + 1 && b1_end == base + 5,
	   "first batch is I3=%s:%lld-%lld (expected %s:%lld-%lld, raw='%s')",
	   b1.uuid.c_str(), (long long)b1_start, (long long)b1_end,
	   expected_uuid.c_str(), (long long)(base + 1), (long long)(base + 5),
	   b1.raw.c_str());

	// Second batch — same uuid, so should be I4.
	for (int i = 6; i <= 10; ++i) {
		if (!db.exec("INSERT INTO binlog_reader_test.batching_t (v) VALUES (" +
		             std::to_string(i) + ")")) {
			BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
		}
	}

	BinlogReaderMsg b2 = client.read_line(2000);
	const trxid_t b2_start = b2.intervals.empty() ? 0 : b2.intervals[0].start;
	const trxid_t b2_end   = b2.intervals.empty() ? 0 : b2.intervals[0].end;
	ok(b2.valid() && b2.kind == "I4" &&
	       b2.intervals.size() == 1 &&
	       b2_start == base + 6 && b2_end == base + 10,
	   "second batch is I4=%lld-%lld (expected %lld-%lld, raw='%s')",
	   (long long)b2_start, (long long)b2_end,
	   (long long)(base + 6), (long long)(base + 10), b2.raw.c_str());

	return exit_status();
}
