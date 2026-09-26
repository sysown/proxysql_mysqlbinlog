#include "mariadb_replication_client.h"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>

#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <mysql.h>
#include <mariadb_rpl.h>

#include "mariadb_replication.h"

namespace {

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

uint32_t mix_server_id(uint64_t value) {
	value ^= value >> 33;
	value *= UINT64_C(0xff51afd7ed558ccd);
	value ^= value >> 33;
	value *= UINT64_C(0xc4ceb9fe1a85ec53);
	value ^= value >> 33;
	return static_cast<uint32_t>(value ^ (value >> 32));
}

bool read_system_entropy(uint32_t* value) {
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return false;

	unsigned char* data = reinterpret_cast<unsigned char*>(value);
	size_t remaining = sizeof(*value);
	while (remaining > 0) {
		const ssize_t bytes = read(fd, data, remaining);
		if (bytes > 0) {
			data += bytes;
			remaining -= static_cast<size_t>(bytes);
			continue;
		}
		if (bytes < 0 && errno == EINTR)
			continue;
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

uint32_t initial_replication_server_id() {
	struct timespec now {};
	if (clock_gettime(CLOCK_REALTIME, &now) != 0)
		now.tv_sec = time(nullptr);

	uint32_t entropy = 0;
	if (!read_system_entropy(&entropy)) {
		entropy = mix_server_id((static_cast<uint64_t>(now.tv_sec) << 32) ^
		                        static_cast<uint64_t>(now.tv_nsec) ^
		                        static_cast<uint64_t>(getpid()));
	}

	const uint32_t id = mix_server_id(
		static_cast<uint64_t>(entropy) ^
		(static_cast<uint64_t>(now.tv_sec) << 32) ^
		static_cast<uint64_t>(now.tv_nsec) ^
		static_cast<uint64_t>(getpid()));
	return id == 0 ? 1 : id;
}

uint32_t next_replication_server_id() {
	static std::atomic<uint32_t> next(initial_replication_server_id());
	uint32_t id = next.fetch_add(1, std::memory_order_relaxed);
	if (id == 0)
		id = next.fetch_add(1, std::memory_order_relaxed);
	return id;
}

}  // namespace

struct MariaDBReplicationClient::Impl {
	explicit Impl(const MariaDBConnectionOptions& value)
	    : options(value), mysql(nullptr), rpl(nullptr), snapshot_position(0),
	      has_snapshot(false), server_id(next_replication_server_id()) {}

	MariaDBConnectionOptions options;
	MYSQL* mysql;
	MARIADB_RPL* rpl;
	std::string snapshot_filename;
	unsigned long snapshot_position;
	bool has_snapshot;
	unsigned int server_id;
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

	if (!validate_replication_timeouts(impl_->options.heartbeat_period_seconds,
	                                  impl_->options.read_timeout_seconds)) {
		std::runtime_error error("invalid replication timeouts: read timeout must be at "
		                         "least three heartbeat periods");
		mysql_close(impl_->mysql);
		impl_->mysql = nullptr;
		throw error;
	}

	std::string tls_error;
	if (!tls_options_valid(impl_->options.tls, &tls_error)) {
		std::runtime_error error("TLS configuration failed: " + tls_error);
		mysql_close(impl_->mysql);
		impl_->mysql = nullptr;
		throw error;
	}
	if (!apply_tls_options(impl_->mysql, impl_->options.tls, &tls_error)) {
		std::runtime_error error("TLS configuration failed: " + tls_error);
		mysql_close(impl_->mysql);
		impl_->mysql = nullptr;
		throw error;
	}

	const unsigned int timeout_seconds = 10;
	mysql_options(impl_->mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_seconds);
	const unsigned int read_timeout_seconds = impl_->options.read_timeout_seconds;
	mysql_options(impl_->mysql, MYSQL_OPT_READ_TIMEOUT, &read_timeout_seconds);
	if (!mysql_real_connect(impl_->mysql, impl_->options.host.c_str(),
	                        impl_->options.user.c_str(), impl_->options.password.c_str(),
	                        nullptr, impl_->options.port, nullptr, 0)) {
		std::runtime_error error = connector_error("mysql_real_connect failed", impl_->mysql);
		mysql_close(impl_->mysql);
		impl_->mysql = nullptr;
		throw error;
	}
	if (!verify_tls_connection(impl_->mysql, impl_->options.tls, &tls_error)) {
		std::runtime_error error("TLS connection failed: " + tls_error);
		mysql_close(impl_->mysql);
		impl_->mysql = nullptr;
		throw error;
	}
}

GTID_Set MariaDBReplicationClient::snapshot() {
	if (!impl_->mysql)
		throw std::runtime_error("replication connection is not initialized");

	const char* query = "SHOW BINARY LOG STATUS";
	if (mysql_query(impl_->mysql, query)) {
		query = "SHOW MASTER STATUS";
		if (mysql_query(impl_->mysql, query))
			throw connector_error("cannot read binary log status", impl_->mysql);
	}

	Result result(mysql_store_result(impl_->mysql));
	if (!result.get())
		throw connector_error("cannot store binary log status", impl_->mysql);
	if (mysql_num_fields(result.get()) < 5)
		throw std::runtime_error(std::string(query) +
		                         " returned fewer than five columns");
	MYSQL_ROW row = mysql_fetch_row(result.get());
	if (!row || !row[0] || !row[1] || !row[4])
		throw std::runtime_error(std::string(query) +
		                         " returned no File, Position, or Executed_Gtid_Set");
	if (!*row[0])
		throw std::runtime_error(std::string(query) +
		                         " returned an empty binary log File");

	unsigned long position = 0;
	if (!parse_mysql_snapshot_position(row[0], row[1], &position))
		throw std::runtime_error(std::string(query) +
		                         " returned an invalid binary log Position");

	GTID_Set set;
	if (!parse_mysql_gtid_executed(row[4], &set))
		throw std::runtime_error(std::string(query) +
		                         " returned an invalid Executed_Gtid_Set");

	impl_->snapshot_filename = row[0];
	impl_->snapshot_position = position;
	impl_->has_snapshot = true;
	return set;
}

void MariaDBReplicationClient::open_stream() {
	if (!impl_->mysql)
		throw std::runtime_error("replication connection is not initialized");
	if (impl_->rpl)
		throw std::runtime_error("replication stream is already open");
	if (!impl_->has_snapshot)
		throw std::runtime_error("replication snapshot has not been captured");

	const std::string heartbeat =
	    heartbeat_statement(impl_->options.heartbeat_period_seconds);
	if (heartbeat.empty())
		throw std::runtime_error("invalid replication heartbeat period");
	if (mysql_query(impl_->mysql, heartbeat.c_str()))
		throw connector_error("cannot enable replication heartbeats", impl_->mysql);

	if (mysql_query(impl_->mysql,
	                "SET @master_binlog_checksum = @@global.binlog_checksum"))
		throw connector_error("cannot enable binary log checksums", impl_->mysql);

	impl_->rpl = mariadb_rpl_init(impl_->mysql);
	if (!impl_->rpl)
		throw std::runtime_error("mariadb_rpl_init failed");

	if (mariadb_rpl_optionsv(impl_->rpl, MARIADB_RPL_FILENAME,
	                         const_cast<char*>(impl_->snapshot_filename.c_str()),
	                         impl_->snapshot_filename.size()) ||
	    mariadb_rpl_optionsv(impl_->rpl, MARIADB_RPL_START,
	                         impl_->snapshot_position) ||
	    mariadb_rpl_optionsv(impl_->rpl, MARIADB_RPL_SERVER_ID,
	                         impl_->server_id)) {
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

uint32_t MariaDBReplicationClient::replication_server_id() const {
	return impl_->server_id;
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

bool validate_replication_timeouts(unsigned int heartbeat_period_seconds,
                                   unsigned int read_timeout_seconds) {
	if (heartbeat_period_seconds == 0 || read_timeout_seconds == 0)
		return false;
	if (heartbeat_period_seconds > UINT_MAX / 3U)
		return false;
	return read_timeout_seconds >= heartbeat_period_seconds * 3U;
}

std::string heartbeat_statement(unsigned int heartbeat_period_seconds) {
	const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
	if (heartbeat_period_seconds == 0)
		return std::string();
	if (static_cast<uint64_t>(heartbeat_period_seconds) >
	    UINT64_MAX / nanoseconds_per_second)
		return std::string();

	const uint64_t period = static_cast<uint64_t>(heartbeat_period_seconds) *
	                        nanoseconds_per_second;
	return "SET @master_heartbeat_period = " + std::to_string(period);
}
