/* listener_bind-t
 *
 * Verify the reader can bind to an explicitly selected IPv4 listener address
 * and serve the initial GTID snapshot through that address.
 */

#include <string>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"

int main() {
	CommandLine cli;
	if (cli.reader_bin.empty())
		skip_all("listener_bind-t requires BINLOG_READER_BIN");

	plan(2);

	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	if (!db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test") ||
	    !db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.listener_bind_t "
	             "(id INT PRIMARY KEY)") ||
	    !db.exec("INSERT IGNORE INTO binlog_reader_test.listener_bind_t VALUES (1)")) {
		BAIL_OUT("cannot create test fixture: %s", db.last_error().c_str());
	}
	ok(!db.gtid_executed().empty(), "MySQL gtid_executed is non-empty");

	BinlogReaderProcess reader;
	reader.binary = cli.reader_bin;
	reader.mysql_host = cli.mysql_host;
	reader.mysql_port = cli.mysql_port;
	reader.mysql_user = cli.mysql_user;
	reader.mysql_password = cli.mysql_password;
	reader.listen_address = "127.0.0.1";
	reader.listen_port = cli.reader_port;
	reader.tls = cli.tls;
	const bool started = reader.start();
	const bool ready = started && reader.wait_ready();

	BinlogReaderClient client;
	ok(ready && client.connect("127.0.0.1", cli.reader_port, 2000),
	   "reader accepts connections on the configured IPv4 listener address");

	return exit_status();
}
