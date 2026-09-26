#include <climits>
#include <cstddef>
#include <string>

#include "tap.h"
#include "mariadb_replication_client.h"

int main() {
	plan(26);
	unsigned int* no_target = NULL;
	unsigned int seconds = 0;
	ok(!parse_positive_seconds("5", no_target), "null destination is rejected");
	ok(parse_positive_seconds("5", &seconds) && seconds == 5,
	   "plain seconds are parsed");
	seconds = 0;
	ok(parse_positive_seconds("1", &seconds) && seconds == 1,
	   "one second is parsed");
	seconds = 0;
	ok(parse_positive_seconds("4294967295", &seconds) && seconds == UINT_MAX,
	   "UINT_MAX is parsed");
	ok(!parse_positive_seconds("", &seconds), "empty value is rejected");
	ok(!parse_positive_seconds("abc", &seconds), "non-digits are rejected");
	ok(!parse_positive_seconds("-1", &seconds), "a sign is rejected");
	ok(!parse_positive_seconds("+1", &seconds), "a leading plus is rejected");
	ok(!parse_positive_seconds(" 5", &seconds), "a leading space is rejected");
	ok(!parse_positive_seconds("5 ", &seconds), "a trailing space is rejected");
	ok(!parse_positive_seconds("5.5", &seconds), "a fraction is rejected");
	ok(!parse_positive_seconds("4294967296", &seconds),
	   "a value beyond UINT_MAX is rejected");
	ok(!parse_positive_seconds(std::string(128, '9'), &seconds),
	   "a very long number is rejected");
	ok(!parse_positive_seconds("0", &seconds), "zero is rejected");
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
