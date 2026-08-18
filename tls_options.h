#ifndef PROXYSQL_MYSQLBINLOG_TLS_OPTIONS_H
#define PROXYSQL_MYSQLBINLOG_TLS_OPTIONS_H

#include <string>

#include <mysql.h>

enum class TLSMode { DISABLED, PREFERRED, REQUIRED };

struct TLSOptions {
	TLSMode mode = TLSMode::REQUIRED;
	bool verify_server_certificate = true;
	std::string ca_file, ca_path, certificate_file, key_file, cipher, version;
};

bool parse_tls_mode(const std::string& value, TLSMode* result);
bool parse_tls_boolean(const std::string& value, bool* result);
bool tls_options_valid(const TLSOptions& options, std::string* error);
bool apply_tls_options(MYSQL* mysql, const TLSOptions& options, std::string* error);
bool verify_tls_connection(MYSQL* mysql, const TLSOptions& options, std::string* error);
const char* tls_mode_name(TLSMode mode);

#endif
