#ifndef PROXYSQL_MARIADB_REPLICATION_CLIENT_H
#define PROXYSQL_MARIADB_REPLICATION_CLIENT_H

#include <cstdint>
#include <functional>
#include <string>

#include "proxysql_gtid.h"
#include "tls_options.h"

struct MariaDBConnectionOptions {
	std::string host;
	unsigned int port;
	std::string user;
	std::string password;
	TLSOptions tls;
	unsigned int heartbeat_period_seconds = 5;
	unsigned int read_timeout_seconds = 60;
};

/**
 * Small ownership wrapper around MariaDB Connector/C's replication API.
 *
 * It starts at the source's binlog position captured with the GTID set it
 * publishes. This mirrors the reader's historical libslave behavior: the
 * listener reports current state, then receives new GTIDs.
 */
class MariaDBReplicationClient {
   public:
	typedef std::function<void(const std::string&, uint64_t)> GTIDCallback;

	explicit MariaDBReplicationClient(const MariaDBConnectionOptions& options);
	~MariaDBReplicationClient();

	MariaDBReplicationClient(const MariaDBReplicationClient&) = delete;
	MariaDBReplicationClient& operator=(const MariaDBReplicationClient&) = delete;

	void connect();
	GTID_Set snapshot();
	void open_stream();
	uint32_t replication_server_id() const;
	void stream_events(const GTIDCallback& on_gtid,
	                   const std::function<bool()>& is_stopping);

	/** Interrupt a blocking replication read without freeing Connector/C state. */
	void interrupt();

   private:
	struct Impl;
	Impl* impl_;
};

/**
 * Reject timeout combinations that would let a silent network partition block
 * mariadb_rpl_fetch forever.  Both values must be positive and the read timeout
 * must cover at least three heartbeat periods.
 */
bool validate_replication_timeouts(unsigned int heartbeat_period_seconds,
                                   unsigned int read_timeout_seconds);

/**
 * Build the statement that makes the source emit HEARTBEAT_LOG_EVENT while the
 * replication stream is idle.  The value is a period in nanoseconds, so an idle
 * partition surfaces as missing events instead of a blocked read.
 *
 * Returns an empty string when the period is zero or would overflow the
 * nanosecond conversion.  The result only ever contains a formatted integer, so
 * there is no injection surface.
 */
std::string heartbeat_statement(unsigned int heartbeat_period_seconds);

/**
 * Pick the most informative message for a failed replication operation.
 *
 * mariadb_rpl_fetch() signals a read timeout by returning NULL and leaving the
 * replication error empty, so the connector error is the only place the real
 * cause (for example CR_SERVER_LOST) is reported.  A non-empty replication
 * message always wins; the connector message is only used as a fallback.  When
 * neither is available the result is "unknown replication error".
 */
std::string replication_error_detail(const char* rpl_message,
                                     const char* connector_message);

#endif  // PROXYSQL_MARIADB_REPLICATION_CLIENT_H
