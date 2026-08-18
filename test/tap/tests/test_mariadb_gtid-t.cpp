/* mariadb_gtid-t
 *
 * The MariaDB Connector/C replication API exposes MySQL GTID UUIDs as 16 raw
 * bytes and returns @@GLOBAL.gtid_executed as a comma-separated string.  The
 * reader's existing wire protocol uses lowercase UUIDs without dashes
 * internally, so the adapter must normalize both representations exactly.
 */

#include <string>

#include "mariadb_replication.h"
#include "proxysql_gtid.h"
#include "tap.h"

int main() {
	plan(6);

	const unsigned char source_id[] = {
		0x24, 0x68, 0x4d, 0x2a, 0x94, 0x12, 0x11, 0xef,
		0x8c, 0x99, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02,
	};
	ok(mysql_uuid_from_bytes(source_id) == "24684d2a941211ef8c990242ac120002",
	   "binary MySQL source ID becomes a dash-free lowercase UUID");

	GTID_Set set;
	ok(parse_mysql_gtid_executed(
	       "24684d2a-9412-11ef-8c99-0242ac120002:1-3:5,"
	       "9c6d6f00-9412-11ef-8c99-0242ac120002:7-9",
	       &set),
	   "parse a multi-UUID executed GTID set");
	ok(set.has_gtid("24684d2a941211ef8c990242ac120002", 1) &&
	       set.has_gtid("24684d2a941211ef8c990242ac120002", 3) &&
	       set.has_gtid("24684d2a941211ef8c990242ac120002", 5),
	   "first UUID preserves sparse intervals");
	ok(!set.has_gtid("24684d2a941211ef8c990242ac120002", 4),
	   "first UUID does not fill a sparse interval gap");
	ok(set.has_gtid("9c6d6f00941211ef8c990242ac120002", 7) &&
	       set.has_gtid("9c6d6f00941211ef8c990242ac120002", 9),
	   "second UUID preserves its interval");

	GTID_Set invalid;
	ok(!parse_mysql_gtid_executed("not-a-gtid", &invalid),
	   "reject malformed executed GTID state");

	return exit_status();
}
