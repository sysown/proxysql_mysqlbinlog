#include "tap.h"
#include "mariadb_replication_client.h"

int main() {
	plan(6);
	ok(validate_replication_timeouts(5, 60), "defaults are valid");
	ok(validate_replication_timeouts(1, 3), "three heartbeat periods are valid");
	ok(!validate_replication_timeouts(0, 60), "zero heartbeat is rejected");
	ok(!validate_replication_timeouts(5, 0), "zero read timeout is rejected");
	ok(!validate_replication_timeouts(5, 14), "timeout below three periods is rejected");
	ok(!validate_replication_timeouts(0, 0), "both zero is rejected");
	return exit_status();
}
