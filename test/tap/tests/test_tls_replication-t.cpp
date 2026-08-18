/* test_tls_replication-t
 *
 * dbdeployer's MySQL sandbox uses a self-signed TLS certificate. The reader
 * must reject it when certificate verification is on, then replicate once
 * verification is deliberately disabled for this test policy.
 */

#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"
#include "tls_options.h"

namespace {

void configure_reader(const CommandLine& cli, int listen_port,
                      const TLSOptions& tls, BinlogReaderProcess* reader) {
	reader->binary = cli.reader_bin;
	reader->mysql_host = cli.mysql_host;
	reader->mysql_port = cli.mysql_port;
	reader->mysql_user = cli.mysql_user;
	reader->mysql_password = cli.mysql_password;
	reader->listen_port = listen_port;
	reader->log_file_path = cli.reader_log_file;
	reader->tls = tls;
}

}  // namespace

int main() {
	CommandLine cli;
	if (cli.reader_bin.empty())
		skip_all("tls_replication-t requires BINLOG_READER_BIN");

	plan(4);

	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	if (!db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test") ||
	    !db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.tls_replication_t "
	             "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)")) {
		BAIL_OUT("cannot create TLS replication fixture: %s",
		         db.last_error().c_str());
	}

	BinlogReaderProcess rejected;
	configure_reader(cli, cli.reader_port + 1, TLSOptions(), &rejected);
	ok(rejected.start(),
	   "reader with REQUIRED TLS certificate verification can be spawned");
	ok(!rejected.wait_ready(5000),
	   "self-signed MySQL certificate is rejected before the reader opens its listener");

	TLSOptions tls;
	tls.mode = TLSMode::REQUIRED;
	tls.verify_server_certificate = false;
	BinlogReaderProcess reader;
	configure_reader(cli, cli.reader_port, tls, &reader);
	const bool reader_ready = reader.start() && reader.wait_ready(15000);
	ok(reader_ready,
	   "reader with REQUIRED TLS and disabled certificate verification listens");
	if (!reader_ready) {
		ok(false, "reader emits a valid I1 after a TLS-protected insert");
		return exit_status();
	}

	BinlogReaderClient client;
	if (!client.connect("127.0.0.1", cli.reader_port, 2000)) {
		diag("cannot connect to reader at 127.0.0.1:%d", cli.reader_port);
		ok(false, "reader emits a valid I1 after a TLS-protected insert");
		return exit_status();
	}

	const BinlogReaderMsg st = client.read_line(10000);
	diag("reader ST message: valid=%d kind='%s' raw='%s' error='%s'",
	     st.valid(), st.kind.c_str(), st.raw.c_str(), st.error.c_str());
	if (!st.valid() || st.kind != "ST") {
		ok(false, "reader emits a valid I1 after a TLS-protected insert");
		return exit_status();
	}

	if (!db.exec("INSERT INTO binlog_reader_test.tls_replication_t (v) VALUES (1)")) {
		diag("TLS replication fixture INSERT failed: %s", db.last_error().c_str());
		ok(false, "reader emits a valid I1 after a TLS-protected insert");
		return exit_status();
	}

	const BinlogReaderMsg update = client.read_line(5000);
	diag("reader update message: valid=%d kind='%s' raw='%s' error='%s'",
	     update.valid(), update.kind.c_str(), update.raw.c_str(),
	     update.error.c_str());
	ok(update.valid() && update.kind == "I1" && !update.uuid.empty() &&
	       update.intervals.size() == 1 && update.intervals[0].start > 0 &&
	       update.intervals[0].start == update.intervals[0].end,
	   "reader emits a valid I1 after a TLS-protected insert");

	return exit_status();
}
