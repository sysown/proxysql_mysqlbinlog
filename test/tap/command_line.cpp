#include "command_line.h"

#include <cstdlib>

static std::string env_str(const char* key, const char* fallback) {
	const char* v = std::getenv(key);
	return (v && *v) ? std::string(v) : std::string(fallback);
}

static int env_int(const char* key, int fallback) {
	const char* v = std::getenv(key);
	if (!v || !*v)
		return fallback;
	return std::atoi(v);
}

static void append_tls_config_error(std::string* error,
	                                const std::string& detail) {
	if (!error->empty())
		*error += "; ";
	*error += detail;
}

CommandLine::CommandLine()
    : mysql_host(env_str("MYSQL_HOST", "127.0.0.1")),
      mysql_port(env_int("MYSQL_PORT", 3306)),
      mysql_user(env_str("MYSQL_USER", "root")),
      mysql_password(env_str("MYSQL_PASSWORD", "root")),
      mysql_version(env_str("MYSQL_VERSION", "")),
      proxy_admin_host(env_str("PROXY_ADMIN_HOST", "127.0.0.1")),
      proxy_admin_port(env_int("PROXY_ADMIN_PORT", 6032)),
      reader_bin(env_str("BINLOG_READER_BIN", "")),
      reader_host(env_str("BINLOG_READER_HOST", "127.0.0.1")),
      reader_port(env_int("BINLOG_READER_PORT", 6020)),
      reader_log_file(env_str("BINLOG_READER_LOG_FILE", "")) {
	const std::string tls_mode = env_str("MYSQL_SSL_MODE", "REQUIRED");
	if (!parse_tls_mode(tls_mode, &tls.mode)) {
		append_tls_config_error(&tls_config_error,
		                        "invalid MYSQL_SSL_MODE='" + tls_mode + "'");
	}

	const std::string verify_server_certificate =
		env_str("MYSQL_SSL_VERIFY_SERVER_CERT", "1");
	if (!parse_tls_boolean(verify_server_certificate,
	                       &tls.verify_server_certificate)) {
		append_tls_config_error(
			&tls_config_error,
			"invalid MYSQL_SSL_VERIFY_SERVER_CERT='" +
				verify_server_certificate + "'");
	}

	tls.ca_file = env_str("MYSQL_SSL_CA", "");
	tls.ca_path = env_str("MYSQL_SSL_CAPATH", "");
	tls.certificate_file = env_str("MYSQL_SSL_CERT", "");
	tls.key_file = env_str("MYSQL_SSL_KEY", "");
	tls.cipher = env_str("MYSQL_SSL_CIPHER", "");
	tls.version = env_str("MYSQL_TLS_VERSION", "");

	std::string validation_error;
	if (tls_config_error.empty() && !tls_options_valid(tls, &validation_error)) {
		tls_config_error = "invalid TLS configuration: " + validation_error;
	}
}
