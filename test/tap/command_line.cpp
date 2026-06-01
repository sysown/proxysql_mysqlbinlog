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

CommandLine::CommandLine()
    : mysql_host(env_str("MYSQL_HOST", "127.0.0.1")),
      mysql_port(env_int("MYSQL_PORT", 3306)),
      mysql_user(env_str("MYSQL_USER", "root")),
      mysql_password(env_str("MYSQL_PASSWORD", "root")),
      mysql_version(env_str("MYSQL_VERSION", "")),
      reader_bin(env_str("BINLOG_READER_BIN", "")),
      reader_host(env_str("BINLOG_READER_HOST", "127.0.0.1")),
      reader_port(env_int("BINLOG_READER_PORT", 6020)),
      reader_log_file(env_str("BINLOG_READER_LOG_FILE", "")),
      proxy_admin_host(env_str("PROXY_ADMIN_HOST", "127.0.0.1")),
      proxy_admin_port(env_int("PROXY_ADMIN_PORT", 6032)) {}
