/* test_replication_server_id-t
 *
 * Every replication client needs its own nonzero server ID.  Constructing the
 * clients without connecting keeps this focused on the identity allocation
 * contract while the startup integration tests cover real dump traffic.
 */

#include <string>

#include "mariadb_replication_client.h"
#include "tap.h"

int main() {
	plan(2);

	MariaDBConnectionOptions options;
	options.host = "127.0.0.1";
	options.port = 3306;
	options.user = "root";
	options.password = "root";

	MariaDBReplicationClient first(options);
	MariaDBReplicationClient second(options);

	ok(first.replication_server_id() != 0 && second.replication_server_id() != 0,
	   "each replication client has a nonzero server ID");
	ok(first.replication_server_id() != second.replication_server_id(),
	   "separate replication clients have distinct server IDs");

	return exit_status();
}
