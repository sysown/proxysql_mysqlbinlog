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
};

#endif
