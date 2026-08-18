/* caching_sha2_auth-t
 *
 * Reproduces the cold caching_sha2_password path used by the reader's
 * replication connection.  The test resets the dedicated replication user
 * immediately before spawning the reader so MySQL cannot use a warmed auth
 * cache. A reader client that cannot complete the cold authentication
 * handshake must not reach the listener or emit GTID messages.
 */

#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "proxysql_gtid.h"
#include "tap.h"
#include "tap_utils.h"

namespace {

const char* const kReaderUser = "binlog_reader_caching_sha2";
const char* const kReaderPassword = "binlog-reader-caching-sha2";

bool configure_reader_user(MySQLClient& db) {
	const std::string account = std::string("'") + kReaderUser + "'@'%'";
	const std::string identified = account +
		" IDENTIFIED WITH caching_sha2_password BY '" + kReaderPassword + "'";

	return db.exec("CREATE USER IF NOT EXISTS " + identified) &&
	       db.exec("ALTER USER " + identified) &&
	       db.exec("GRANT REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO " +
	               account) &&
	       db.exec("FLUSH PRIVILEGES");
}

std::string start_caching_sha2_reader(const CommandLine& cli,
	                                  BinlogReaderProcess& reader) {
	if (cli.reader_bin.empty()) {
		diag("caching_sha2_auth-t requires spawn mode");
		return "";
	}

	reader.binary = cli.reader_bin;
	reader.mysql_host = cli.mysql_host;
	reader.mysql_port = cli.mysql_port;
	reader.mysql_user = kReaderUser;
	reader.mysql_password = kReaderPassword;
	reader.listen_port = cli.reader_port;
	reader.foreground = true;
	if (!cli.reader_log_file.empty())
		reader.log_file_path = cli.reader_log_file;

	if (!reader.start() || !reader.wait_ready(15000))
		return "";

	return "127.0.0.1";
}

}  // namespace

int main() {
	CommandLine cli;
	if (cli.mysql_version == "57")
		skip_all("caching_sha2_password is unavailable on MySQL 5.7");

	plan(3);

	diag("target MySQL %s:%d (version=%s)", cli.mysql_host.c_str(),
	     cli.mysql_port, cli.mysql_version.empty() ? "?" : cli.mysql_version.c_str());

	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());

	if (!configure_reader_user(db))
		BAIL_OUT("cannot configure %s: %s", kReaderUser, db.last_error().c_str());

	if (!db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test") ||
	    !db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.caching_sha2_auth_t "
	             "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)")) {
		BAIL_OUT("cannot create test fixture: %s", db.last_error().c_str());
	}

	BinlogReaderProcess reader;
	const std::string reader_host = start_caching_sha2_reader(cli, reader);
	ok(!reader_host.empty(),
	   "reader accepts connections after cold caching_sha2_password authentication");
	if (reader_host.empty())
		return exit_status();

	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000)) {
		BAIL_OUT("cannot connect to reader at %s:%d", reader_host.c_str(),
		         cli.reader_port);
	}

	const BinlogReaderMsg st = client.read_line(10000);
	ok(st.valid() && st.kind == "ST" && !st.uuid.empty() && !st.intervals.empty(),
	   "cold-authenticated reader emits ST (raw='%s')", st.raw.c_str());
	if (!st.valid() || st.kind != "ST")
		return exit_status();

	if (!db.exec("INSERT INTO binlog_reader_test.caching_sha2_auth_t (v) VALUES (1)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());

	const BinlogReaderMsg update = client.read_line(5000);
	const trxid_t trxid = update.intervals.empty() ? 0 : update.intervals[0].start;
	ok(update.valid() && update.kind == "I1" &&
	       update.uuid == strip_dashes(st.uuid) && update.intervals.size() == 1 &&
	       trxid > 0,
	   "cold-authenticated reader emits I1 after a write (raw='%s')",
	   update.raw.c_str());

	return exit_status();
}
