#ifndef PROXYSQL_MARIADB_REPLICATION_CLIENT_H
#define PROXYSQL_MARIADB_REPLICATION_CLIENT_H

#include <cstdint>
#include <functional>
#include <string>

#include "proxysql_gtid.h"

struct MariaDBConnectionOptions {
	std::string host;
	unsigned int port;
	std::string user;
	std::string password;
};

/**
 * Small ownership wrapper around MariaDB Connector/C's replication API.
 *
 * It starts at the source's current binlog position after taking a snapshot
 * of @@GLOBAL.gtid_executed. This mirrors the reader's historical libslave
 * behavior: the listener reports current state, then receives new GTIDs.
 */
class MariaDBReplicationClient {
   public:
	typedef std::function<void(const std::string&, uint64_t)> GTIDCallback;

	explicit MariaDBReplicationClient(const MariaDBConnectionOptions& options);
	~MariaDBReplicationClient();

	MariaDBReplicationClient(const MariaDBReplicationClient&) = delete;
	MariaDBReplicationClient& operator=(const MariaDBReplicationClient&) = delete;

	void connect();
	GTID_Set executed_gtid_set();
	void open_stream();
	void stream_events(const GTIDCallback& on_gtid,
	                   const std::function<bool()>& is_stopping);

	/** Interrupt a blocking replication read without freeing Connector/C state. */
	void interrupt();

   private:
	struct Impl;
	Impl* impl_;
};

#endif  // PROXYSQL_MARIADB_REPLICATION_CLIENT_H
