/* mariadb_gtid-t
 *
 * The MariaDB Connector/C replication API exposes MySQL GTID UUIDs as 16 raw
 * bytes and returns @@GLOBAL.gtid_executed as a comma-separated string.  The
 * reader's existing wire protocol uses lowercase UUIDs without dashes
 * internally, so the adapter must normalize both representations exactly.
 */

#include <cstdint>
#include <limits>
#include <string>

#include "mariadb_replication.h"
#include "proxysql_gtid.h"
#include "tap.h"

int main() {
	plan(49);

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

	GTID_Set whitespace_set;
	ok(parse_mysql_gtid_executed(
	       "24684d2a-9412-11ef-8c99-0242ac120002:1-3,\n\t"
	       "9c6d6f00-9412-11ef-8c99-0242ac120002:7-9",
	       &whitespace_set) &&
	       whitespace_set.has_gtid("9c6d6f00941211ef8c990242ac120002", 9),
	   "parse whitespace-separated multi-UUID executed GTID set");

	GTID_Set invalid;
	ok(!parse_mysql_gtid_executed("not-a-gtid", &invalid),
	   "reject malformed executed GTID state");

	unsigned long snapshot_position = 0;
	ok(parse_mysql_snapshot_position("mysql-bin.000001", "4", &snapshot_position)
	       && snapshot_position == 4,
	   "parses a valid binary log snapshot position");
	ok(!parse_mysql_snapshot_position("", "4", &snapshot_position),
	   "rejects an empty binary log file");
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", "", &snapshot_position),
	   "rejects an empty binary log position");
	// strtoul() alone accepts leading whitespace, but the protocol field is decimal ASCII.
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", " 4", &snapshot_position),
	   "rejects a non-ASCII-decimal binary log position");
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", "+4", &snapshot_position),
	   "rejects a signed positive binary log position");
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", "-4", &snapshot_position),
	   "rejects a signed negative binary log position");
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", "4x", &snapshot_position),
	   "rejects an alphabetic binary log position");
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", "3", &snapshot_position),
	   "rejects a binary log position below the first valid offset");
	const std::string overflowing_position =
		std::to_string(std::numeric_limits<unsigned long>::max()) + "0";
	ok(!parse_mysql_snapshot_position("mysql-bin.000001", overflowing_position.c_str(),
					  &snapshot_position),
	   "rejects an overflowing binary log position");

	GTID_Set mdb;
	ok(parse_mariadb_gtid_executed("0-1-270", &mdb), "parse MariaDB single GTID");
	ok(mdb.has_gtid("0", 1) && mdb.has_gtid("0", 270) && !mdb.has_gtid("0", 271),
	   "MariaDB snapshot is watermark [1, seq]");
	ok(!mdb.has_gtid("1", 270), "other domain is absent");

	GTID_Set mdb_set;
	ok(parse_mariadb_gtid_executed("0-1-270,1-2-50", &mdb_set)
	       && mdb_set.has_gtid("0", 100) && mdb_set.has_gtid("1", 50),
	   "parse MariaDB multi-domain set");

	GTID_Set combined;
	ok(parse_gtid_executed("0-1-270", &combined) && combined.has_gtid("0", 270),
	   "combined parser accepts MariaDB");
	ok(parse_gtid_executed(
	       "24684d2a-9412-11ef-8c99-0242ac120002:1-3", &combined)
	       && combined.has_gtid("24684d2a941211ef8c990242ac120002", 3),
	   "combined parser still accepts MySQL");

	GTID_Set bad;
	ok(!parse_mariadb_gtid_executed("0-1", &bad), "reject two-field MariaDB");
	ok(!parse_mariadb_gtid_executed("0-1-0", &bad), "reject sequence 0");
	ok(!parse_mariadb_gtid_executed("00-1-1", &bad), "reject leading zeros");
	ok(!parse_mariadb_gtid_executed("0-1-1:2", &bad), "reject colon in MariaDB");
	GTID_Set empty_position;
	ok(parse_mariadb_gtid_executed("", &empty_position)
	       && empty_position.map.empty() && empty_position.last_server_id.empty(),
	   "parse empty MariaDB position as empty set");
	ok(!parse_mariadb_gtid_executed("0-1-270,not-a-gtid", &bad),
	   "reject mixed junk");

	// domain_id and server_id are uint32 on the wire (GTID_EVENT body), so a
	// snapshot text carrying more than 32 bits cannot have come from a server
	// this reader can stream, and would be silently truncated on the cast.
	GTID_Set oversized;
	ok(!parse_mariadb_gtid_executed("0-4294967296-1", &oversized),
	   "reject a MariaDB server id above UINT32_MAX");
	ok(!parse_mariadb_gtid_executed("4294967296-1-1", &oversized),
	   "reject a MariaDB domain id above UINT32_MAX");
	ok(oversized.map.empty() && oversized.last_server_id.empty(),
	   "an oversized token leaves no partial position behind");

	GTID_Set boundary;
	ok(parse_mariadb_gtid_executed("4294967295-4294967295-1", &boundary)
	       && boundary.has_gtid("4294967295", 1)
	       && boundary.get_server_id("4294967295") == UINT32_MAX,
	   "accept the largest uint32 domain and server id");

	GTID_Set wire;
	parse_mariadb_gtid_executed("0-1-270", &wire);
	ok(wire.to_string() == "0:1-270", "wire to_string is domain:1-seq");
	ok(wire.to_display_string() == "0-1-270",
	   "display string keeps MariaDB native form");
	wire.add("0", trxid_t(271));
	ok(wire.to_string() == "0:1-271", "incremental seq extends watermark");

	// bench_gtid_callback() treats a false GTID_Set::add() as "this sequence
	// is already in the position": no last_trx_id/last_server_uuid update and
	// no I1/I2 queued, because ST= already covers it.
	GTID_Set dedup;
	parse_mariadb_gtid_executed("0-1-270", &dedup);
	ok(!dedup.add("0", trxid_t(270)), "re-adding a GTID already in the set is rejected");
	ok(dedup.to_string() == "0:1-270", "a rejected duplicate leaves the position unchanged");
	ok(dedup.add("0", trxid_t(271)), "a new sequence is still accepted");

	GTID_Set s;
	ok(snapshot_gtid_set("24684d2a-9412-11ef-8c99-0242ac120002:1-3", "", &s)
	       && s.has_gtid("24684d2a941211ef8c990242ac120002", 3),
	   "non-empty MySQL fifth column wins");
	ok(!snapshot_gtid_set("not-a-gtid", "0-1-270", &s),
	   "malformed MySQL fifth column does not fall through");
	ok(snapshot_gtid_set(nullptr, "0-1-270", &s) && s.has_gtid("0", 270),
	   "missing fifth column uses MariaDB binlog pos");
	ok(snapshot_gtid_set("", "0-1-270", &s) && s.map.empty(),
	   "empty fifth column is empty MySQL set");
	ok(snapshot_gtid_set(nullptr, "", &s) && s.map.empty(),
	   "empty MariaDB binlog pos is an empty snapshot set");

	ok(is_mariadb_server("10.11.18-MariaDB-ubu2204-log"),
	   "flavor detect accepts a MariaDB version banner");
	ok(is_mariadb_server("5.5.5-10.11.18-MariaDB"),
	   "flavor detect accepts a replication-prefixed MariaDB banner");
	ok(!is_mariadb_server("8.0.36"), "flavor detect rejects MySQL");
	ok(!is_mariadb_server("8.0.36-0ubuntu0.22.04.1"),
	   "flavor detect rejects a plain MySQL build string");
	ok(!is_mariadb_server(nullptr), "flavor detect rejects a null banner");
	ok(!is_mariadb_server(""), "flavor detect rejects an empty banner");

	return exit_status();
}
