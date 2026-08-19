/* test_snapshot_startup-t
 *
 * A reader must stream every transaction committed after the GTID snapshot it
 * publishes in ST=.  The inherited test delay keeps the replication stream
 * closed after that snapshot, providing a deterministic startup window:
 *
 *   1. start the reader with a test-only post-snapshot delay;
 *   2. receive ST= (the captured snapshot is now observable);
 *   3. commit one INSERT while the stream is still closed;
 *   4. require that the reader later emits that transaction.
 *
 * The production default has no delay.  This test is spawn-only because the
 * environment variable must be inherited by the reader process.
 */

#include <cstdlib>
#include <limits>
#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"

namespace {

const char* const kPostSnapshotDelayEnv =
	"PROXYSQL_BINLOG_READER_TEST_AFTER_SNAPSHOT_DELAY_MS";
const char* const kPostSnapshotDelayMs = "5000";

}  // namespace

int main() {
	CommandLine cli;
	if (cli.reader_bin.empty())
		skip_all("snapshot_startup-t requires spawn mode");

	plan(3);

	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());

	if (!db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test") ||
	    !db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.snapshot_startup_t "
	             "(id INT PRIMARY KEY AUTO_INCREMENT)")) {
		BAIL_OUT("cannot create test fixture: %s", db.last_error().c_str());
	}
	if (!db.exec("INSERT INTO binlog_reader_test.snapshot_startup_t VALUES ()"))
		BAIL_OUT("cannot seed GTID state: %s", db.last_error().c_str());
	if (db.gtid_executed().empty())
		BAIL_OUT("seed transaction did not create an executed GTID");

	if (setenv(kPostSnapshotDelayEnv, kPostSnapshotDelayMs, 1) != 0)
		BAIL_OUT("cannot set %s", kPostSnapshotDelayEnv);

	BinlogReaderProcess reader;
	const std::string reader_host = setup_reader(cli, reader);
	unsetenv(kPostSnapshotDelayEnv);
	if (reader_host.empty())
		BAIL_OUT("failed to start reader with %s", kPostSnapshotDelayEnv);

	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000)) {
		BAIL_OUT("cannot connect to reader at %s:%d", reader_host.c_str(),
		         cli.reader_port);
	}

	const BinlogReaderMsg st = client.read_line(5000);
	ok(st.valid() && st.kind == "ST" && !st.uuid.empty() && !st.intervals.empty(),
	   "reader publishes its captured GTID snapshot (raw='%s')", st.raw.c_str());
	if (!st.valid() || st.kind != "ST")
		return exit_status();

	trxid_t snapshot_max = 0;
	for (const auto& interval : st.intervals) {
		if (interval.end > snapshot_max)
			snapshot_max = interval.end;
	}
	if (snapshot_max > std::numeric_limits<trxid_t>::max() - 1000)
		BAIL_OUT("snapshot GTID is too large for the test transaction");
	const trxid_t expected_trxid = snapshot_max + 1000;
	const std::string expected_gtid = st.uuid + ":" +
	                                  std::to_string(expected_trxid);
	if (!db.exec("SET GTID_NEXT='" + expected_gtid + "'") ||
	    !db.exec("INSERT INTO binlog_reader_test.snapshot_startup_t VALUES ()") ||
	    !db.exec("SET GTID_NEXT='AUTOMATIC'")) {
		BAIL_OUT("cannot commit the expected GTID transaction: %s",
		         db.last_error().c_str());
	}

	const BinlogReaderMsg early = client.read_line(1000);
	ok(!early.valid() && early.error == "timeout",
	   "test-only post-snapshot delay keeps the stream closed before the INSERT "
	   "(kind='%s', error='%s', raw='%s')",
	   early.kind.c_str(), early.error.c_str(), early.raw.c_str());

	const BinlogReaderMsg update = client.read_line(8000);
	ok(update.valid() && update.kind == "I1" && update.intervals.size() == 1 &&
	       update.uuid == strip_dashes(st.uuid) &&
	       update.intervals[0].start == expected_trxid &&
	       update.intervals[0].end == expected_trxid,
	   "reader emits the expected post-ST GTID %s (raw='%s')",
	   expected_gtid.c_str(), update.raw.c_str());

	return exit_status();
}
