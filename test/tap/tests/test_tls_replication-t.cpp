/* test_tls_replication-t
 *
 * dbdeployer's MySQL sandbox uses a self-signed TLS certificate. The reader
 * must reject it when certificate verification is on, then replicate once
 * verification is deliberately disabled for this test policy.
 */

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h>

#include "binlog_reader_client.h"
#include "binlog_reader_process.h"
#include "command_line.h"
#include "mysql_client.h"
#include "tap.h"
#include "tap_utils.h"
#include "tls_options.h"

namespace {

class TemporaryLogFile {
   public:
	TemporaryLogFile() {
		char filename[] = "/tmp/test_tls_replication.XXXXXX";
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

	TemporaryLogFile(const TemporaryLogFile&) = delete;
	TemporaryLogFile& operator=(const TemporaryLogFile&) = delete;

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

bool is_certificate_verification_failure(const std::string& output) {
	return output.find("TLS/SSL error: Certificate verification failure") !=
	           std::string::npos ||
	       output.find("TLS/SSL error: self-signed certificate in certificate chain") !=
	           std::string::npos;
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

	TemporaryLogFile strict_reader_log;
	if (!strict_reader_log.valid())
		BAIL_OUT("cannot create strict-reader log: %s",
		         strict_reader_log.error().c_str());

	BinlogReaderProcess rejected;
	configure_reader(cli, cli.reader_port + 1, TLSOptions(), &rejected);
	rejected.log_file_path = strict_reader_log.path();
	const bool strict_started = rejected.start();
	ok(strict_started,
	   "reader with REQUIRED TLS certificate verification can be spawned");
	const bool strict_listener_opened =
		strict_started && rejected.wait_ready(5000);
	const bool strict_reader_reaped = strict_started && !rejected.running();
	const std::string strict_reader_output = strict_reader_log.contents();
	diag("strict reader log: %s", strict_reader_output.c_str());
	ok(strict_started && !strict_listener_opened && strict_reader_reaped &&
	       is_certificate_verification_failure(strict_reader_output),
	   "self-signed MySQL certificate verification rejects the reader before its listener opens");

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
