# Issue 48 Heartbeat and Read Timeout Design

## Problem

Since the move to MariaDB Connector/C, a silent network partition leaves `mariadb_rpl_fetch()` blocked forever. The reader remains alive and serves its last GTID set, so ProxySQL can treat a stale position as current.

## Design

Add two positive, configurable replication-liveness settings:

- `--heartbeat-period`: default 5 seconds; sent as `@master_heartbeat_period` in nanoseconds before `mariadb_rpl_open()`.
- `--read-timeout`: default 60 seconds; passed to Connector/C as `MYSQL_OPT_READ_TIMEOUT` before `mysql_real_connect()`.

The read timeout must be at least three heartbeat periods and no greater than `INT_MAX / 1000` seconds (2,147,483), because Connector/C stores the timeout in milliseconds in a signed integer. This lets an idle source survive between heartbeats while bounding detection of a dead link. Connector/C already returns heartbeat events; the existing GTID callback ignores non-GTID events. A stream error follows the current path: log the error, stop the listener, and let the supervisor restart the reader.

The settings live in `MariaDBConnectionOptions` so the replication client owns the protocol behavior and the CLI only supplies validated values. No wire protocol change is required.

## Testing

- Pure TAP tests for timeout validation and the exact heartbeat SQL statement.
- CLI/startup TAP test passes explicit options and still receives `ST=`.
- Existing replication-failure TAP still observes nonzero exit and the stream error log.
- Full reader TAP build and existing MySQL matrix remain green.

The silent-partition reproduction requires network-namespace/iptables privileges and is documented as an integration follow-up unless the harness can provide that environment.

## Files

`mariadb_replication_client.{h,cpp}`, `proxysql_binlog_reader.cpp`, `README.md`, reader TAP process wrapper, timeout/startup/failure TAP tests, and the timeout test Makefile rule.
