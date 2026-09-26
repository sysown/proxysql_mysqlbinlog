/* replication_failure_shutdown-t
 *
 * A replication-stream error happens after the reader starts its TCP server.
 * The reader must stop that server and exit rather than blocking forever while
 * joining its event-loop thread.
 */

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h>

#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"

namespace {

const char* const kReaderUser = "binlog_reader_stream_failure";
const char* const kReaderPassword = "binlog-reader-stream-failure";
const char* const kPostSnapshotDelayEnv =
	"PROXYSQL_BINLOG_READER_TEST_AFTER_SNAPSHOT_DELAY_MS";

class TemporaryLogFile {
   public:
	TemporaryLogFile() {
		char filename[] = "/tmp/test_replication_failure_shutdown.XXXXXX";
		const int fd = mkstemp(filename);
		if (fd < 0) {
			error_ = strerror(errno);
			return;
		}
		if (close(fd) != 0) {
			error_ = strerror(errno);
			unlink(filename);
			return;
		}
		path_ = filename;
	}

	~TemporaryLogFile() {
		if (!path_.empty())
			unlink(path_.c_str());
	}

	bool valid() const { return !path_.empty(); }
	const std::string& error() const { return error_; }
	const std::string& path() const { return path_; }

	std::string contents() const {
		std::ifstream input(path_.c_str());
		return std::string(std::istreambuf_iterator<char>(input),
		                   std::istreambuf_iterator<char>());
	}

   private:
	std::string path_;
	std::string error_;
};

bool configure_limited_reader_user(MySQLClient& db) {
	const std::string account = std::string("'") + kReaderUser + "'@'%'";
	const std::string identified = account + " IDENTIFIED BY '" +
	                               kReaderPassword + "'";
	return db.exec("CREATE USER IF NOT EXISTS " + identified) &&
	       db.exec("ALTER USER " + identified) &&
	       db.exec("GRANT REPLICATION CLIENT ON *.* TO " + account) &&
	       db.exec("FLUSH PRIVILEGES");
}

}  // namespace

int main() {
	CommandLine cli;
	if (cli.reader_bin.empty())
		skip_all("replication_failure_shutdown-t requires BINLOG_READER_BIN");

	plan(3);

	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect to MySQL: %s", db.last_error().c_str());
	if (!db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test") ||
	    !db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.replication_failure_shutdown_t "
	             "(id INT PRIMARY KEY AUTO_INCREMENT)") ||
	    !db.exec("INSERT INTO binlog_reader_test.replication_failure_shutdown_t VALUES ()")) {
		BAIL_OUT("cannot create test fixture: %s", db.last_error().c_str());
	}
	if (!configure_limited_reader_user(db))
		BAIL_OUT("cannot configure limited reader user: %s", db.last_error().c_str());

	TemporaryLogFile reader_log;
	if (!reader_log.valid())
		BAIL_OUT("cannot create reader log: %s", reader_log.error().c_str());

	if (setenv(kPostSnapshotDelayEnv, "1000", 1) != 0)
		BAIL_OUT("cannot set %s", kPostSnapshotDelayEnv);

	BinlogReaderProcess reader;
	reader.binary = cli.reader_bin;
	reader.mysql_host = cli.mysql_host;
	reader.mysql_port = cli.mysql_port;
	reader.mysql_user = kReaderUser;
	reader.mysql_password = kReaderPassword;
	reader.listen_port = cli.reader_port;
	reader.log_file_path = reader_log.path();
	reader.tls = cli.tls;
	reader.heartbeat_period_seconds = 1;
	reader.read_timeout_seconds = 5;
	const bool reader_started = reader.start();
	unsetenv(kPostSnapshotDelayEnv);
	// Startup is dominated by the binlog snapshot plus the injected
	// post-snapshot delay before the listener is opened. Measured at
	// roughly 8-9s on the 5.7/8.0/8.4/9.4 fleet, so 5s expires before
	// the listener ever exists. Keep this generous; the exit/error
	// assertions below carry the actual test signal.
	const bool reader_ready = reader_started && reader.wait_ready(15000);
	ok(reader_ready,
	   "reader opens its listener before the replication stream is rejected");
	if (!reader_ready) {
		ok(false, "reader exits when its replication stream fails");
		ok(false, "reader logs its replication stream failure");
		return exit_status();
	}

	int exit_code = -1;
	const bool reader_exited = reader.wait_exit(5000, &exit_code);
	ok(reader_exited && exit_code != 0,
	   "reader exits when its replication stream fails (exited=%d code=%d)",
	   reader_exited, exit_code);

	const std::string output = reader_log.contents();
	diag("reader log: %s", output.c_str());
	ok(reader_exited && output.find("Error in reading binlogs:") != std::string::npos,
	   "reader logs its replication stream failure");

	return exit_status();
}
