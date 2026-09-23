#ifndef PROXYSQL_MARIADB_REPLICATION_H
#define PROXYSQL_MARIADB_REPLICATION_H

#include <string>

#include "proxysql_gtid.h"

/** Convert a MySQL UUID in binary log-event form to this project's raw form. */
std::string mysql_uuid_from_bytes(const unsigned char* source_id);

/** Parse MySQL's @@GLOBAL.gtid_executed representation into a GTID_Set. */
bool parse_mysql_gtid_executed(const std::string& encoded, GTID_Set* out);

bool parse_mariadb_gtid_executed(const std::string& encoded, GTID_Set* out);
bool parse_gtid_executed(const std::string& encoded, GTID_Set* out);

/** Validate a binary log snapshot File and decimal Position. */
bool parse_mysql_snapshot_position(const char* filename, const char* encoded_position,
                                   unsigned long* out);

#endif  // PROXYSQL_MARIADB_REPLICATION_H
