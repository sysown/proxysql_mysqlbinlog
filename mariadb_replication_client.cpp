#include "mariadb_replication_client.h"

#include <cstdlib>
#include <stdexcept>

#include <sys/socket.h>

#include <mysql.h>
#include <mariadb_rpl.h>

#include "mariadb_replication.h"

namespace {

const unsigned int kReplicationServerId = 6020;

std::runtime_error connector_error(const char* action, MYSQL* mysql) {
	return std::runtime_error(std::string(action) + ": " + ::mysql_error(mysql));
}

std::runtime_error rpl_error(const char* action, MARIADB_RPL* rpl) {
	return std::runtime_error(std::string(action) + ": " + mariadb_rpl_error(rpl));
}

class Result {
   public:
	explicit Result(MYSQL_RES* value) : value_(value) {}
	~Result() { mysql_free_result(value_); }

	Result(const Result&) = delete;
	Result& operator=(const Result&) = delete;

	MYSQL_RES* get() const { return value_; }

   private:
	MYSQL_RES* value_;
};

}  // namespace

struct MariaDBReplicationClient::Impl {
	explicit Impl(const MariaDBConnectionOptions& value)
	    : options(value), mysql(nullptr), rpl(nullptr) {}

	MariaDBConnectionOptions options;
	MYSQL* mysql;
	MARIADB_RPL* rpl;
};

MariaDBReplicationClient::MariaDBReplicationClient(
	const MariaDBConnectionOptions& options)
	: impl_(new Impl(options)) {}

MariaDBReplicationClient::~MariaDBReplicationClient() {
	if (!impl_)
		return;
	if (impl_->rpl)
		mariadb_rpl_close(impl_->rpl);
	if (impl_->mysql)
		mysql_close(impl_->mysql);
	delete impl_;
}

void MariaDBReplicationClient::connect() {
	if (impl_->mysql)
		return;

	impl_->mysql = mysql_init(nullptr);
	if (!impl_->mysql)
		throw std::runtime_error("mysql_init failed");

	const unsigned int timeout_seconds = 10;
	mysql_options(impl_->mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_seconds);
	if (!mysql_real_connect(impl_->mysql, impl_->options.host.c_str(),
	                        impl_->options.user.c_str(), impl_->options.password.c_str(),
	                        nullptr, impl_->options.port, nullptr, 0)) {
		std::runtime_error error = connector_error("mysql_real_connect failed", impl_->mysql);
		mysql_close(impl_->mysql);
		impl_->mysql = nullptr;
		throw error;
	}
}

GTID_Set MariaDBReplicationClient::executed_gtid_set() {
	if (!impl_->mysql)
		throw std::runtime_error("replication connection is not initialized");
	if (mysql_query(impl_->mysql, "SELECT @@GLOBAL.gtid_executed"))
		throw connector_error("cannot read @@GLOBAL.gtid_executed", impl_->mysql);

	Result result(mysql_store_result(impl_->mysql));
	if (!result.get())
		throw connector_error("cannot store @@GLOBAL.gtid_executed", impl_->mysql);
	MYSQL_ROW row = mysql_fetch_row(result.get());
	if (!row || !row[0])
		throw std::runtime_error("@@GLOBAL.gtid_executed returned no value");

	GTID_Set set;
	if (!parse_mysql_gtid_executed(row[0], &set))
		throw std::runtime_error("cannot parse @@GLOBAL.gtid_executed");
	return set;
}

void MariaDBReplicationClient::open_stream() {
	if (!impl_->mysql)
		throw std::runtime_error("replication connection is not initialized");
	if (impl_->rpl)
		throw std::runtime_error("replication stream is already open");
	if (mysql_query(impl_->mysql,
	                "SET @master_binlog_checksum = @@global.binlog_checksum"))
		throw connector_error("cannot enable binary log checksums", impl_->mysql);
	// MySQL 8.4 removed SHOW MASTER STATUS. Older MySQL releases (and
	// MariaDB) retain the legacy spelling, so try it only when the new one
	// is unavailable.
	if (mysql_query(impl_->mysql, "SHOW BINARY LOG STATUS") &&
	    mysql_query(impl_->mysql, "SHOW MASTER STATUS"))
		throw connector_error("cannot read binary log status", impl_->mysql);

	Result result(mysql_store_result(impl_->mysql));
	if (!result.get())
		throw connector_error("cannot store master status", impl_->mysql);
	MYSQL_ROW row = mysql_fetch_row(result.get());
	if (!row || !row[0] || !row[1])
		throw std::runtime_error("SHOW MASTER STATUS returned no binary log position");

	char* end = nullptr;
	const unsigned long position = std::strtoul(row[1], &end, 10);
	if (!end || *end != '\0' || position < 4)
		throw std::runtime_error("SHOW MASTER STATUS returned an invalid binary log position");

	impl_->rpl = mariadb_rpl_init(impl_->mysql);
	if (!impl_->rpl)
		throw std::runtime_error("mariadb_rpl_init failed");

	const std::string filename(row[0]);
	if (mariadb_rpl_optionsv(impl_->rpl, MARIADB_RPL_FILENAME,
	                         const_cast<char*>(filename.c_str()), filename.size()) ||
	    mariadb_rpl_optionsv(impl_->rpl, MARIADB_RPL_START, position) ||
	    mariadb_rpl_optionsv(impl_->rpl, MARIADB_RPL_SERVER_ID,
	                         kReplicationServerId)) {
		std::runtime_error error = rpl_error("cannot configure replication stream", impl_->rpl);
		mariadb_rpl_close(impl_->rpl);
		impl_->rpl = nullptr;
		throw error;
	}
	if (mariadb_rpl_open(impl_->rpl)) {
		std::runtime_error error = rpl_error("cannot open replication stream", impl_->rpl);
		mariadb_rpl_close(impl_->rpl);
		impl_->rpl = nullptr;
		throw error;
	}
}

void MariaDBReplicationClient::stream_events(
	const GTIDCallback& on_gtid, const std::function<bool()>& is_stopping) {
	if (!impl_->rpl)
		throw std::runtime_error("replication stream is not open");

	MARIADB_RPL_EVENT* event = nullptr;
	while (!is_stopping() && (event = mariadb_rpl_fetch(impl_->rpl, event))) {
		if (event->event_type == GTID_LOG_EVENT && on_gtid) {
			on_gtid(mysql_uuid_from_bytes(
			            reinterpret_cast<const unsigned char*>(event->event.gtid_log.source_id)),
		        event->event.gtid_log.sequence_nr);
		}
	}
	mariadb_free_rpl_event(event);

	if (!is_stopping())
		throw rpl_error("replication stream ended", impl_->rpl);
}

void MariaDBReplicationClient::interrupt() {
	if (!impl_ || !impl_->mysql)
		return;
	const my_socket socket = mysql_get_socket(impl_->mysql);
	if (socket != MARIADB_INVALID_SOCKET)
		shutdown(static_cast<int>(socket), SHUT_RDWR);
}
