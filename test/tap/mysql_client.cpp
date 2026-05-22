#include "mysql_client.h"

#include "command_line.h"
#include "tap.h"

MySQLClient::~MySQLClient() {
	if (mysql_) {
		mysql_close(mysql_);
		mysql_ = nullptr;
	}
}

/**
 * Open a connection using the host/port/user/password from cli.
 *
 * @param cli Test configuration with the connection fields.
 *
 * @return true on success; false if mysql_init or mysql_real_connect
 *         failed (see last_error()).
 */
bool MySQLClient::connect(const CommandLine& cli) {
	if (mysql_) {
		mysql_close(mysql_);
		mysql_ = nullptr;
	}

	mysql_ = mysql_init(nullptr);
	if (!mysql_) {
		last_error_ = "mysql_init failed";
		return false;
	}

	if (!mysql_real_connect(mysql_, cli.mysql_host.c_str(),
	                        cli.mysql_user.c_str(),
	                        cli.mysql_password.c_str(), nullptr, cli.mysql_port,
	                        nullptr, 0)) {
		last_error_ = mysql_error(mysql_);
		mysql_close(mysql_);
		mysql_ = nullptr;
		return false;
	}
	host_ = cli.mysql_host;
	port_ = cli.mysql_port;
	user_ = cli.mysql_user;
	return true;
}

/**
 * Execute one SQL statement, draining any result-set so the
 * connection stays usable for the next call. Emits a TAP diag line
 * with host/port/user/query/status.
 *
 * @param sql The statement to send.
 *
 * @return true if mysql_query returned 0; on false, last_error()
 *         carries the server-reported error text.
 */
bool MySQLClient::exec(const std::string& sql) {
	if (!mysql_) {
		last_error_ = "not connected";
		diag("host=%s port=%d user=%s query='%s' status=FAIL error='not connected'",
		     host_.c_str(), port_, user_.c_str(), sql.c_str());
		return false;
	}
	if (mysql_query(mysql_, sql.c_str()) != 0) {
		last_error_ = mysql_error(mysql_);
		diag("host=%s port=%d user=%s query='%s' status=FAIL error='%s'",
		     host_.c_str(), port_, user_.c_str(), sql.c_str(), last_error_.c_str());
		return false;
	}
	// Drain any result-set to keep the connection usable for the next query.
	MYSQL_RES* r = mysql_store_result(mysql_);
	if (r)
		mysql_free_result(r);
	diag("host=%s port=%d user=%s query='%s' status=PASS",
	     host_.c_str(), port_, user_.c_str(), sql.c_str());
	return true;
}

/**
 * Wipe binary logs and reset gtid_executed. Tests call this when they
 * need predictable, gap-free trxid assignment under AUTOMATIC mode —
 * sparse-t deliberately leaves gaps in gtid_executed which would break
 * "next GTID = max + 1" assumptions in sibling tests.
 *
 * Tries the 8.4+ form first; falls back to the 5.7/8.0 form on
 * failure.
 *
 * @return true if either form succeeded.
 */
bool MySQLClient::reset_gtid_set() {
	return exec("RESET BINARY LOGS AND GTIDS") || exec("RESET MASTER");
}

/**
 * Read `@@global.gtid_executed` from the server.
 *
 * @return The serialized GTID set (typically "uuid:N-M" or
 *         multi-interval "uuid:N-M:P-Q"). Empty string on error.
 */
std::string MySQLClient::gtid_executed() {
	if (!mysql_) {
		last_error_ = "not connected";
		return "";
	}
	if (mysql_query(mysql_, "SELECT @@global.gtid_executed") != 0) {
		last_error_ = mysql_error(mysql_);
		return "";
	}
	MYSQL_RES* r = mysql_store_result(mysql_);
	if (!r) {
		last_error_ = mysql_error(mysql_);
		return "";
	}
	std::string out;
	MYSQL_ROW row = mysql_fetch_row(r);
	if (row && row[0])
		out = row[0];
	mysql_free_result(r);
	return out;
}
