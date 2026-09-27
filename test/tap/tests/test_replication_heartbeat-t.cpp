/* replication_heartbeat-t
 *
 * Issue 48: an idle replication stream must not be torn down by the
 * reader's read timeout.
 *
 * The reader is started with a 1s heartbeat period and a 5s read
 * timeout. A client stays connected and idle for 7s — longer than the
 * read timeout — and the reader must still be running when an INSERT
 * finally arrives on the stream.
 *
 * This only covers the idle-stream case: the source heartbeat keeps
 * feeding the replication connection so the read timeout never fires.
 * It does not reproduce a network partition.
 */

#include <chrono>
#include <string>
#include <thread>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"

int main() {
	CommandLine cli;
	// Spawn mode only: the test asserts on the reader *process*, so an
	// externally-running reader at reader_host:reader_port is no use.
	if (cli.reader_bin.empty())
		skip_all("replication_heartbeat-t requires BINLOG_READER_BIN");

	plan(3);

	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());

	if (!db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test") ||
	    !db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.replication_heartbeat_t "
	             "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)")) {
		BAIL_OUT("cannot create test fixture: %s", db.last_error().c_str());
	}

	BinlogReaderProcess reader;
	// setup_reader() only overwrites the fields it maps from cli, so
	// these two survive.
	reader.heartbeat_period_seconds = 1;
	reader.read_timeout_seconds = 5;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty())
		BAIL_OUT("failed to start %s", cli.reader_bin.c_str());

	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000))
		BAIL_OUT("cannot connect to reader at %s:%d", reader_host.c_str(),
		         cli.reader_port);

	BinlogReaderMsg st = client.read_line(10000);
	if (!st.valid() || st.kind != "ST") {
		diag("read_line: raw='%s', error=%s, errno=%d", st.raw.c_str(),
		     st.error.c_str(), st.last_errno);
	}
	ok(st.valid() && st.kind == "ST" && !st.uuid.empty() && !st.intervals.empty(),
	   "first line is ST: '%s'", st.raw.c_str());
	if (!st.valid() || st.kind != "ST") {
		skip(2, "no ST= line; the rest of the test cannot run");
		return exit_status();
	}

	// Idle for longer than the 5s read timeout. Without a source
	// heartbeat the reader would hit that timeout and exit here.
	diag("idling 7000ms (heartbeat=1s, read timeout=5s)");
	std::this_thread::sleep_for(std::chrono::milliseconds(7000));

	// A short wait_exit() probe actually reaps the child, so unlike
	// running() it fails when the reader died during the idle period.
	// When the reader is alive it returns false and leaves pid_ intact,
	// so the I1 read below stays the authoritative liveness check.
	int exit_code = -1;
	int term_signal = 0;
	const bool reader_exited = reader.wait_exit(100, &exit_code, &term_signal);
	if (reader_exited) {
		diag("reader exited during the idle period (exit_code=%d term_signal=%d)",
		     exit_code, term_signal);
	}
	ok(!reader_exited,
	   "reader did not exit during 7000ms idle with a 5s read timeout (pid=%d)",
	   reader.pid());

	if (!db.exec("INSERT INTO binlog_reader_test.replication_heartbeat_t (v) VALUES (1)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());

	BinlogReaderMsg m1 = client.read_line(10000);
	const trxid_t got1 = m1.intervals.empty() ? 0 : m1.intervals[0].start;
	ok(m1.valid() && m1.kind == "I1" && m1.intervals.size() == 1 && got1 > 0,
	   "update after the idle period is I1=%s:%lld (raw='%s')",
	   m1.uuid.c_str(), (long long)got1, m1.raw.c_str());

	return exit_status();
}
