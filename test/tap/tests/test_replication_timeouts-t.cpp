#include <climits>

#include "tap.h"
#include "mariadb_replication_client.h"

int main() {
	plan(12);
	ok(validate_replication_timeouts(5, 60), "defaults are valid");
	ok(validate_replication_timeouts(1, 3), "three heartbeat periods are valid");
	ok(!validate_replication_timeouts(0, 60), "zero heartbeat is rejected");
	ok(!validate_replication_timeouts(5, 0), "zero read timeout is rejected");
	ok(!validate_replication_timeouts(5, 14), "timeout below three periods is rejected");
	ok(!validate_replication_timeouts(0, 0), "both zero is rejected");
	ok(!validate_replication_timeouts(UINT_MAX / 3U + 1U, UINT_MAX),
	   "overflowing heartbeat validation is rejected");
	ok(heartbeat_statement(5) == "SET @master_heartbeat_period = 5000000000",
	   "heartbeat statement uses nanoseconds");
	ok(heartbeat_statement(1) == "SET @master_heartbeat_period = 1000000000",
	   "one-second heartbeat statement");
	ok(replication_error_detail("rpl failed", "mysql failed") == "rpl failed",
	   "replication message wins over the connector message");
	ok(replication_error_detail("", "mysql failed") == "mysql failed",
	   "empty replication message falls back to the connector message");
	ok(replication_error_detail(nullptr, nullptr) == "unknown replication error",
	   "null and empty messages yield the unknown replication error");
	return exit_status();
}
