#ifndef BINLOG_READER_TEST_MYSQL_CLIENT_H
#define BINLOG_READER_TEST_MYSQL_CLIENT_H

#include <mysql/mysql.h>

#include <string>

class CommandLine;

/**
 * Thin libmysqlclient connection wrapper for binlog_reader tests.
 *
 * Holds one MySQL connection. Every exec() emits a TAP diag line
 * `host=... port=... user=... query='...' status=PASS|FAIL` so each
 * test's SQL trail is visible in the TAP output.
 */
class MySQLClient {
   public:
	MySQLClient() = default;
	~MySQLClient();

	MySQLClient(const MySQLClient&) = delete;
	MySQLClient& operator=(const MySQLClient&) = delete;

	/**
	 * Open a connection using the host/port/user/password from cli.
	 *
	 * @param cli Test configuration with the connection fields.
	 *
	 * @return true on success; false if mysql_init or
	 *         mysql_real_connect failed (see last_error()).
	 */
	bool connect(const CommandLine& cli);

	/**
	 * Execute one SQL statement, draining any result-set so the
	 * connection stays usable for the next call. Emits a TAP diag
	 * line with host/port/user/query/status.
	 *
	 * @param sql The statement to send.
	 *
	 * @return true if mysql_query returned 0; on false, last_error()
	 *         carries the server-reported error text.
	 */
	bool exec(const std::string& sql);

	/**
	 * Read `@@global.gtid_executed` from the server.
	 *
	 * @return The serialized GTID set (typically "uuid:N-M" or
	 *         multi-interval "uuid:N-M:P-Q"). Empty string on error.
	 */
	std::string gtid_executed();

	/**
	 * Wipe binary logs and reset gtid_executed to empty. Tests call
	 * this when they need predictable, gap-free trxid assignment under
	 * AUTOMATIC mode — sparse-t deliberately leaves gaps in
	 * gtid_executed which would break "next GTID = max + 1"
	 * assumptions in sibling tests.
	 *
	 * Tries the 8.4+ form `RESET BINARY LOGS AND GTIDS` first; on
	 * failure (5.7/8.0 reject the new syntax) falls back to
	 * `RESET MASTER`.
	 *
	 * @return true if either form succeeded.
	 */
	bool reset_gtid_set();

	/** Server-reported error text from the most recent failed call. */
	const std::string& last_error() const { return last_error_; }

	/** Raw libmysqlclient handle for escape-hatch use. */
	MYSQL* raw() { return mysql_; }

   private:
	MYSQL* mysql_ = nullptr;
	std::string last_error_;

	// Connection details
	std::string host_;
	int port_ = 0;
	std::string user_;
};

#endif
