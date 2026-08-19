/* test_batched_updates-t
 *
 * With the reader in batched mode (-b 1 -t N), multiple INSERTs that
 * are reported through I3/I4 batched updates. The timer phase is
 * independent of the test's INSERT loop, so one group may be emitted as
 * several contiguous I3/I4 lines; together, those lines must cover the
 * complete group.
 *
 *   1. Reset GTID state so trxids are predictable.
 *   2. Start reader with batching=1 and freq_ms=300.
 *   3. Read ST=, capture baseline.
 *   4. Fire four INSERTs, cross a timer boundary, then fire the fifth.
 *   5. Read updates through baseline+5 — first must be I3 and the range
 *      must be contiguous and complete.
 *   6. Repeat for five more INSERTs.
 *   7. Read updates through baseline+10 — all must be I4 (same uuid
 *      implied) and the range must be contiguous and complete.
 */

#include <string>
#include <chrono>
#include <thread>
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

struct BatchedRange {
	bool complete = false;
	bool aggregated = false;
	std::string error;
	std::vector<std::string> raw_lines;
};

static BatchedRange read_batched_range(BinlogReaderClient& client,
	                                   const std::string& expected_uuid,
	                                   trxid_t first_trxid,
	                                   trxid_t last_trxid,
	                                   bool first_line_is_i3) {
	BatchedRange result;
	trxid_t next_trxid = first_trxid;
	bool first_line = true;

	while (next_trxid <= last_trxid) {
		BinlogReaderMsg msg = client.read_line(2000);
		result.raw_lines.push_back(msg.raw);
		if (!msg.valid()) {
			result.error = "reader error: " + msg.error;
			return result;
		}

		const std::string expected_kind =
			(first_line && first_line_is_i3) ? "I3" : "I4";
		if (msg.kind != expected_kind) {
			result.error = "expected " + expected_kind + ", got " + msg.kind;
			return result;
		}
		if ((msg.kind == "I3" && msg.uuid != expected_uuid) ||
		    msg.intervals.size() != 1) {
			result.error = "unexpected UUID or interval count";
			return result;
		}

		const TrxId_Interval& interval = msg.intervals[0];
		if (interval.start != next_trxid || interval.end > last_trxid) {
			result.error = "non-contiguous or out-of-range interval";
			return result;
		}
		result.aggregated = result.aggregated || interval.end > interval.start;
		next_trxid = interval.end + 1;
		first_line = false;
	}

	result.complete = true;
	return result;
}

static std::string join_lines(const std::vector<std::string>& lines) {
	std::string joined;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i != 0) joined += ", ";
		joined += lines[i];
	}
	return joined;
}

static void cross_timer_boundary(int frequency_ms) {
	std::this_thread::sleep_for(
		std::chrono::milliseconds(frequency_ms + 100));
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

	if (!cli.reader_bin.empty())  // reset gtid only in spawn mode
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

	// Deliberately split the five updates across timer windows. The first
	// interval must still batch the first four, and together the emitted
	// I3/I4 lines must cover every transaction without a gap.
	for (int i = 1; i <= 4; ++i) {
		if (!db.exec("INSERT INTO binlog_reader_test.batching_t (v) VALUES (" +
		             std::to_string(i) + ")")) {
			BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
		}
	}
	cross_timer_boundary(cli.freq_ms);
	if (!db.exec("INSERT INTO binlog_reader_test.batching_t (v) VALUES (5)")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}

	BatchedRange b1 = read_batched_range(client, expected_uuid, base + 1,
	                                     base + 5, true);
	ok(b1.complete && b1.aggregated,
	   "first batch covers %lld-%lld in I3/I4 updates (raw='%s'; error='%s')",
	   (long long)(base + 1), (long long)(base + 5),
	   join_lines(b1.raw_lines).c_str(), b1.error.c_str());

	// Second batch — same uuid, so every line should be I4.
	for (int i = 6; i <= 9; ++i) {
		if (!db.exec("INSERT INTO binlog_reader_test.batching_t (v) VALUES (" +
		             std::to_string(i) + ")")) {
			BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
		}
	}
	cross_timer_boundary(cli.freq_ms);
	if (!db.exec("INSERT INTO binlog_reader_test.batching_t (v) VALUES (10)")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}

	BatchedRange b2 = read_batched_range(client, expected_uuid, base + 6,
	                                     base + 10, false);
	ok(b2.complete && b2.aggregated,
	   "second batch covers %lld-%lld in I4 updates (raw='%s'; error='%s')",
	   (long long)(base + 6), (long long)(base + 10),
	   join_lines(b2.raw_lines).c_str(), b2.error.c_str());

	return exit_status();
}
