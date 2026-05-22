#ifndef BINLOG_READER_TEST_COMMAND_LINE_H
#define BINLOG_READER_TEST_COMMAND_LINE_H

#include <string>

class CommandLine {
   public:
	CommandLine();

	std::string mysql_host;
	int mysql_port;
	std::string mysql_user;
	std::string mysql_password;
	std::string mysql_version;

	std::string proxy_admin_host;
	int proxy_admin_port;

	std::string reader_bin;
	std::string reader_host;
	int reader_port;
	std::string reader_log_file;

	// Reader runtime knobs. Default to -1 (use the reader's own default);
	// tests that need a specific value (e.g. batching-t) set them on
	// their local CommandLine before calling setup_reader().
	int batching = -1;
	int freq_ms = -1;
};

#endif
