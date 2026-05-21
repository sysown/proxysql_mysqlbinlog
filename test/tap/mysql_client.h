#ifndef BINLOG_READER_TEST_MYSQL_CLIENT_H
#define BINLOG_READER_TEST_MYSQL_CLIENT_H

#include <mysql/mysql.h>

#include <string>

class CommandLine;

/* Thin libmysqlclient connection wrapper for binlog_reader tests. */
class MySQLClient {
   public:
	MySQLClient() = default;
	~MySQLClient();

	MySQLClient(const MySQLClient&) = delete;
	MySQLClient& operator=(const MySQLClient&) = delete;

	// Connects using values from CommandLine. Returns true on success.
	bool connect(const CommandLine& cli);

	// Executes a single statement (no result-set retrieval). Returns true
	// if mysql_query returned 0. On failure, error text is available via
	// last_error().
	bool exec(const std::string& sql);

	// Read GTID_EXECUTED via SELECT @@global.gtid_executed. Returns empty
	// string on error.
	std::string gtid_executed();

	const std::string& last_error() const { return last_error_; }
	MYSQL* raw() { return mysql_; }

   private:
	MYSQL* mysql_ = nullptr;
	std::string last_error_;
};

#endif
