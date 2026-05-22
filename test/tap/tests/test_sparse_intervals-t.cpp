/* test_sparse_intervals-t
 *
 * Use SET GTID_NEXT to create gaps in GTID_EXECUTED so it serializes
 * as multiple intervals. Verify the reader's ST= line carries the
 * exact same multi-interval set.
 *
 *   1. Read current gtid_executed; compute a baseline (max trxid).
 *   2. SET GTID_NEXT for several trxids with gaps, COMMIT each as an
 *      empty transaction. End with GTID_NEXT='AUTOMATIC'.
 *   3. Read gtid_executed again; it now has multiple intervals.
 *   4. Start reader; read ST=; raw must equal "ST=" + gtid_executed.
 */

#include <algorithm>
#include <string>
#include <vector>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "proxysql_gtid.h"
#include "tap.h"
#include "tap_utils.h"

// Return the highest trxid mentioned in a "<uuid>:N-M[:N-M[:...]]" string.
// Used to pick offsets that won't collide with existing GTIDs.
static trxid_t max_trxid_in(const std::string& gtid_executed) {
	const size_t first_colon = gtid_executed.find(':');
	if (first_colon == std::string::npos) return 0;

	trxid_t mx = 0;
	size_t pos = first_colon + 1;
	while (pos < gtid_executed.size()) {
		const size_t next = gtid_executed.find(':', pos);
		std::string piece = (next == std::string::npos)
		                        ? gtid_executed.substr(pos)
		                        : gtid_executed.substr(pos, next - pos);
		TrxId_Interval iv(piece);
		if (iv.end > mx) mx = iv.end;
		if (next == std::string::npos) break;
		pos = next + 1;
	}
	return mx;
}

int main() {
	plan(2);

	CommandLine cli;
	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli)) {
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	}

	// Force a non-empty gtid_executed so the uuid is present.
	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");

	std::string before = db.gtid_executed();
	if (before.empty()) {
		BAIL_OUT("gtid_executed is empty even after DDL");
	}
	const std::string uuid = before.substr(0, before.find(':'));
	const trxid_t base = max_trxid_in(before);
	diag("uuid=%s, baseline trxid=%lld", uuid.c_str(), (long long)base);

	// Plan three new intervals separated by gaps:
	//   [base+10, base+12]   skip 13,14
	//   [base+15, base+16]   skip 17
	//   [base+18, base+20]
	const std::vector<trxid_t> targets = {
	    base + 10, base + 11, base + 12,
	    base + 15, base + 16,
	    base + 18, base + 19, base + 20,
	};

	for (trxid_t t : targets) {
		const std::string g = uuid + ":" + std::to_string(t);
		if (!db.exec("SET GTID_NEXT='" + g + "'") ||
		    !db.exec("BEGIN") ||
		    !db.exec("COMMIT")) {
			BAIL_OUT("failed to commit %s: %s", g.c_str(),
			         db.last_error().c_str());
		}
	}
	db.exec("SET GTID_NEXT='AUTOMATIC'");

	const std::string expected = db.gtid_executed();
	// One ':' separates the uuid from the first interval; each subsequent
	// ':' starts another interval (in MySQL's serialization).
	const long mysql_interval_count = std::count(expected.begin(), expected.end(), ':');
	ok(mysql_interval_count >= 4,
	   "gtid_executed now has %ld intervals (expected >=4): %s",
	   mysql_interval_count, expected.c_str());

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
	trxid_t max_end = 0;
	for (auto& iv : st.intervals) {
		if (iv.end > max_end) max_end = iv.end;
	}
	ok(st.valid() && st.kind == "ST" &&
	       static_cast<long>(st.intervals.size()) == mysql_interval_count &&
	       max_end == base + 20,
	   "ST has %zu intervals, max trxid=%lld (expected %ld intervals, max=%lld; raw='%s')",
	   st.intervals.size(), (long long)max_end,
	   mysql_interval_count, (long long)(base + 20), st.raw.c_str());

	return exit_status();
}
