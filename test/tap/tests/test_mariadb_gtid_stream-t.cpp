/* mariadb_gtid_stream-t
 *
 * Live MariaDB snapshot and stream. Skip unless VERSION() contains MariaDB.
 * CREATE happens before setup_reader so @@gtid_binlog_pos is non-empty.
 */

#include <mysql.h>

#include <fstream>
#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"
#include "wire_contract.h"

/**
 * Finds the wire-contract fixture. run.sh runs the binaries from test/tap, but
 * a developer or a CI job may invoke one from the repository root, so try the
 * plausible roots rather than assuming a working directory.
 */
static std::string resolve_fixture() {
	const char *candidates[] = {
		"wire_contract/mariadb_gtid_wire.txt",
		"test/tap/wire_contract/mariadb_gtid_wire.txt",
		"../wire_contract/mariadb_gtid_wire.txt",
	};
	for (const char *candidate : candidates) {
		std::ifstream probe(candidate);
		if (probe) {
			return candidate;
		}
	}
	return candidates[0];
}

/**
 * Asserts that a live line the reader sent matches the fixture's template for
 * that kind, and that the fixture still has a template for every kind seen.
 *
 * This is the reader half of the cross-repository wire contract. The sequence
 * numbers are server-dependent, so only the literal skeleton is compared; see
 * test/tap/wire_contract/mariadb_gtid_wire.txt.
 */
static void check_wire_contract(const std::string &fixture_path, const std::string &kind,
                                const std::string &raw, int *failures) {
	std::vector<WireContractRecord> records;
	if (!load_wire_contract(fixture_path, &records)) {
		diag("Bail out! cannot read wire contract fixture %s", fixture_path.c_str());
		(*failures)++;
		return;
	}
	for (const auto &record : records) {
		if (record.kind != kind) {
			continue;
		}
		if (!wire_contract::matches(record.templ, raw)) {
			diag("wire contract: reader sent '%s' which does not match the "
			     "fixture template '%s' for %s",
			     raw.c_str(), record.templ.c_str(), kind.c_str());
			(*failures)++;
		}
		return;
	}
	diag("wire contract: no template for %s in the fixture; the reader emitted "
	     "a message kind ProxySQL was never told about",
	     kind.c_str());
	(*failures)++;
}

int main() {
	plan(4);

	CommandLine cli;
	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect: %s", db.last_error().c_str());
	if (mysql_query(db.raw(), "SELECT VERSION()"))
		BAIL_OUT("SELECT VERSION failed: %s", mysql_error(db.raw()));
	MYSQL_RES *ver_res = mysql_store_result(db.raw());
	MYSQL_ROW ver_row = ver_res ? mysql_fetch_row(ver_res) : nullptr;
	std::string ver = (ver_row && ver_row[0]) ? ver_row[0] : "";
	mysql_free_result(ver_res);
	if (ver.find("MariaDB") == std::string::npos) {
		skip(4, "not MariaDB (version=%s)", ver.c_str());
		return exit_status();
	}

	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.mdb_gtid_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)");

	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty())
		BAIL_OUT("failed to start reader");
	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000))
		BAIL_OUT("cannot connect to reader");

	// The fixture is shared verbatim with the ProxySQL repository, which replays
	// it through its own parser.
	const std::string fixture = resolve_fixture();
	int contract_failures = 0;

	BinlogReaderMsg st = client.read_line(10000);
	ok(st.valid() && st.kind == "ST" && st.uuid == "0" && !st.intervals.empty()
	       && st.intervals[0].start == 1,
	   "MariaDB ST= is domain 0 watermark (raw='%s')", st.raw.c_str());
	if (st.valid() && st.kind == "ST") {
		check_wire_contract(fixture, "ST", st.raw, &contract_failures);
	}

	if (!db.exec("INSERT INTO binlog_reader_test.mdb_gtid_t (v) VALUES (1)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	BinlogReaderMsg m1 = client.read_line(5000);
	ok(m1.valid() && m1.kind == "I1" && m1.uuid == "0" &&
	       m1.intervals.size() == 1 && m1.intervals[0].start > 0,
	   "INSERT emits I1=0:<seq> (raw='%s')", m1.raw.c_str());
	if (m1.valid() && m1.kind == "I1") {
		check_wire_contract(fixture, "I1", m1.raw, &contract_failures);
	}

	if (!db.exec("INSERT INTO binlog_reader_test.mdb_gtid_t (v) VALUES (2)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	BinlogReaderMsg m2 = client.read_line(5000);
	ok(m2.valid() && m2.kind == "I2" &&
	       m2.intervals.size() == 1 &&
	       m2.intervals[0].start == m1.intervals[0].start + 1,
	   "second INSERT emits I2=<seq+1> (raw='%s')", m2.raw.c_str());
	if (m2.valid() && m2.kind == "I2") {
		check_wire_contract(fixture, "I2", m2.raw, &contract_failures);
	}

	ok(contract_failures == 0,
	   "reader output matches the shared ProxySQL wire contract fixture");

	return exit_status();
}
