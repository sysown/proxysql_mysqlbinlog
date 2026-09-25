#ifndef PROXYSQL_MARIADB_REPLICATION_H
#define PROXYSQL_MARIADB_REPLICATION_H

#include <string>

#include "proxysql_gtid.h"

/** Convert a MySQL UUID in binary log-event form to this project's raw form. */
std::string mysql_uuid_from_bytes(const unsigned char* source_id);

/** Parse MySQL's @@GLOBAL.gtid_executed representation into a GTID_Set. */
bool parse_mysql_gtid_executed(const std::string& encoded, GTID_Set* out);

bool parse_mariadb_gtid_executed(const std::string& encoded, GTID_Set* out);

/**
 * Parse a GTID set of either flavor: MySQL `uuid:intervals` when the text
 * contains a colon, MariaDB `domain-server-sequence` otherwise.
 */
bool parse_gtid_executed(const std::string& encoded, GTID_Set* out);

/**
 * Resolve the GTID set of a snapshot that has no MySQL executed column.
 * `executed_gtid_set_or_null` is the MySQL-shaped fifth column, which is
 * always NULL on the fallback path; a present column is auto-detected by
 * parse_gtid_executed() instead.
 */
bool snapshot_gtid_set(const char* executed_gtid_set_or_null,
                       const std::string& mariadb_binlog_pos,
                       GTID_Set* out);

/** Whether a server_version banner identifies a MariaDB server. */
bool is_mariadb_server(const char* server_version);

/** Validate a binary log snapshot File and decimal Position. */
bool parse_mysql_snapshot_position(const char* filename, const char* encoded_position,
                                   unsigned long* out);

#endif  // PROXYSQL_MARIADB_REPLICATION_H
