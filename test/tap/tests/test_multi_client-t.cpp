/* test_multi_client-t
 *
 * The reader fans the same update stream out to all connected clients.
 * With N concurrent BinlogReaderClient connections, each INSERT must
 * produce one matching line on every client.
 *
 *   1. Reset GTID state so trxids are predictable.
 *   2. Start reader (streaming mode, default).
 *   3. Open N=5 client connections; drain each one's ST=.
 *   4. INSERT once → every client's next line must be the same I1.
 *   5. INSERT again → every client's next line must be the same I2.
 */

#include <memory>
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

	CommandLine cli;
	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli)) {
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	}

	db.reset_gtid_set();

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.broadcast_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT)");

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty()) {
		BAIL_OUT("failed to start reader");
	}

	const int N = 5;
	std::vector<std::unique_ptr<BinlogReaderClient>> clients;
	for (int i = 0; i < N; ++i) {
		clients.emplace_back(new BinlogReaderClient());
		if (!clients.back()->connect(reader_host, cli.reader_port, 2000)) {
			BAIL_OUT("client %d: connect failed", i);
		}
		BinlogReaderMsg st = clients.back()->read_line(5000);
		if (!st.valid() || st.kind != "ST") {
			BAIL_OUT("client %d: didn't get ST (error='%s', raw='%s')",
			         i, st.error.c_str(), st.raw.c_str());
		}
	}

	// INSERT 1 — every client should see the same I1.
	if (!db.exec("INSERT INTO binlog_reader_test.broadcast_t VALUES ()")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}

	trxid_t common_trxid_1 = 0;
	std::string common_uuid_1;
	bool all_match_1 = true;
	for (int i = 0; i < N; ++i) {
		BinlogReaderMsg m = clients[i]->read_line(5000);
		if (!m.valid() || m.kind != "I1" || m.intervals.size() != 1) {
			all_match_1 = false;
			diag("client %d: kind='%s' error='%s' raw='%s'",
			     i, m.kind.c_str(), m.error.c_str(), m.raw.c_str());
			continue;
		}
		if (i == 0) {
			common_trxid_1 = m.intervals[0].start;
			common_uuid_1 = m.uuid;
		} else if (m.intervals[0].start != common_trxid_1 ||
		           m.uuid != common_uuid_1) {
			all_match_1 = false;
			diag("client %d diverges: trxid=%lld uuid='%s' (want %lld, '%s')",
			     i, (long long)m.intervals[0].start, m.uuid.c_str(),
			     (long long)common_trxid_1, common_uuid_1.c_str());
		}
	}
	ok(all_match_1,
	   "all %d clients received the same I1=%s:%lld",
	   N, common_uuid_1.c_str(), (long long)common_trxid_1);

	// INSERT 2 — every client should see the same I2.
	if (!db.exec("INSERT INTO binlog_reader_test.broadcast_t VALUES ()")) {
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	}

	trxid_t common_trxid_2 = 0;
	bool all_match_2 = true;
	for (int i = 0; i < N; ++i) {
		BinlogReaderMsg m = clients[i]->read_line(5000);
		if (!m.valid() || m.kind != "I2" || m.intervals.size() != 1) {
			all_match_2 = false;
			diag("client %d: kind='%s' error='%s' raw='%s'",
			     i, m.kind.c_str(), m.error.c_str(), m.raw.c_str());
			continue;
		}
		if (i == 0) {
			common_trxid_2 = m.intervals[0].start;
		} else if (m.intervals[0].start != common_trxid_2) {
			all_match_2 = false;
			diag("client %d diverges: trxid=%lld (want %lld)",
			     i, (long long)m.intervals[0].start, (long long)common_trxid_2);
		}
	}
	ok(all_match_2 && common_trxid_2 == common_trxid_1 + 1,
	   "all %d clients received the same I2=%lld (expected %lld)",
	   N, (long long)common_trxid_2, (long long)(common_trxid_1 + 1));

	return exit_status();
}
