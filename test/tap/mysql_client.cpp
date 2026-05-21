#include "mysql_client.h"

#include "command_line.h"

MySQLClient::~MySQLClient() {
	if (mysql_) {
		mysql_close(mysql_);
		mysql_ = nullptr;
	}
}

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
	return true;
}

bool MySQLClient::exec(const std::string& sql) {
	if (!mysql_) {
		last_error_ = "not connected";
		return false;
	}
	if (mysql_query(mysql_, sql.c_str()) != 0) {
		last_error_ = mysql_error(mysql_);
		return false;
	}
	// Drain any result-set to keep the connection usable for the next query.
	MYSQL_RES* r = mysql_store_result(mysql_);
	if (r)
		mysql_free_result(r);
	return true;
}

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
