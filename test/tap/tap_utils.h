#ifndef BINLOG_READER_TEST_TAP_UTILS_H
#define BINLOG_READER_TEST_TAP_UTILS_H

#include <unistd.h>

#include <string>

#include "tap.h"
#include "command_line.h"
#include "binlog_reader_process.h"

/**
 * Spawn the reader locally if cli.reader_bin is set; otherwise use the
 * externally-running reader at cli.reader_host:cli.reader_port.
 *
 * @param cli    Test configuration. cli.batching and cli.freq_ms (both
 *               default -1) are propagated to the spawned reader; set
 *               them on the local CommandLine before calling if a
 *               specific mode is required.
 * @param reader Process wrapper. In spawn mode, its fields are
 *               populated from cli and the process is started + waited
 *               for ready. In connect mode, it is left untouched.
 *
 * @return Host string to pass to client.connect(): "127.0.0.1" in spawn
 *         mode, cli.reader_host in connect mode. Empty on any failure.
 */
inline std::string setup_reader(const CommandLine &cli, BinlogReaderProcess &reader) {
	std::string reader_host;
	const bool spawn_mode = !cli.reader_bin.empty();

	if (spawn_mode) {
		reader.binary = cli.reader_bin;
		reader.mysql_host = cli.mysql_host;
		reader.mysql_port = cli.mysql_port;
		reader.mysql_user = cli.mysql_user;
		reader.mysql_password = cli.mysql_password;
		reader.listen_port = cli.reader_port;
		if (cli.batching >= 0) reader.batching = cli.batching;
		if (cli.freq_ms >= 0) reader.freq_ms = cli.freq_ms;
		if (!cli.reader_log_file.empty()) {
			reader.log_file_path = cli.reader_log_file;
		}

		if (!reader.start()) {
			diag("failed to start %s", cli.reader_bin.c_str());
			return "";
		}
		if (!reader.wait_ready(15000)) {
			return "";
		}
		diag("reader listener accepting on port %d", cli.reader_port);
		reader_host = "127.0.0.1";
	} else {
		diag("connect mode: skipping spawn (reader expected at %s:%d)",
		   cli.reader_host.c_str(), cli.reader_port);
		reader_host = cli.reader_host;
		// Give the external reader time to catch up the DB setup before
		// the client connects.
		sleep(3);
	}

	return reader_host;
}

/**
 * Return a copy of `s` with every '-' removed.
 */
inline std::string strip_dashes(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		if (c != '-') out.push_back(c);
	}
	return out;
}

#endif
