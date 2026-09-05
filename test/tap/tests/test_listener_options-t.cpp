/* listener_options-t
 *
 * Exercise -l parsing before the reader attempts to connect to MySQL.  Valid
 * listener specifications reach the deliberately unavailable MySQL endpoint;
 * invalid specifications must instead be reported as ordinary CLI errors.
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>

#include <unistd.h>

#include "command_line.h"
#include "tap.h"

namespace {

struct CommandResult {
	int exit_code;
	std::string output;
};

CommandResult run_reader(const std::string& binary, const std::string& listener) {
	char output_name[] = "/tmp/test_listener_options.XXXXXX";
	const int output_fd = mkstemp(output_name);
	if (output_fd < 0)
		BAIL_OUT("mkstemp failed: %s", strerror(errno));
	close(output_fd);

	const pid_t child = fork();
	if (child < 0)
		BAIL_OUT("fork failed: %s", strerror(errno));
	if (child == 0) {
		const int output = open(output_name, O_WRONLY | O_TRUNC);
		if (output < 0)
			_exit(127);
		if (dup2(output, STDOUT_FILENO) < 0 ||
		    dup2(output, STDERR_FILENO) < 0)
			_exit(127);
		close(output);
		execl(binary.c_str(), binary.c_str(), "-f", "-h", "127.0.0.1",
		      "-P", "1", "-u", "listener_test", "-l", listener.c_str(),
		      static_cast<char*>(nullptr));
		_exit(127);
	}

	int status = 0;
	if (waitpid(child, &status, 0) != child)
		BAIL_OUT("waitpid failed: %s", strerror(errno));

	FILE* output = fopen(output_name, "r");
	std::string contents;
	if (output) {
		char buffer[1024];
		while (fgets(buffer, sizeof(buffer), output))
			contents += buffer;
		fclose(output);
	}
	unlink(output_name);

	return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, contents};
}

bool reaches_mysql_connection(const CommandResult& result) {
	return result.exit_code == 1 &&
	       result.output.find("Error in initializing replication client:") !=
	           std::string::npos;
}

bool is_cli_error(const CommandResult& result) {
	return result.exit_code == 1 &&
	       result.output.find("invalid listener address:") !=
	           std::string::npos &&
	       result.output.find("Usage:") != std::string::npos;
}

}  // namespace

int main() {
	CommandLine cli;
	if (cli.reader_bin.empty())
		skip_all("listener_options-t requires BINLOG_READER_BIN");

	plan(8);

	CommandResult ipv4 = run_reader(cli.reader_bin, "127.0.0.1:6020");
	ok(reaches_mysql_connection(ipv4),
	   "IPv4 listener address reaches MySQL connection (exit=%d, output=%s)",
	   ipv4.exit_code, ipv4.output.c_str());

	CommandResult ipv6 = run_reader(cli.reader_bin, "[::1]:6020");
	ok(reaches_mysql_connection(ipv6),
	   "bracketed IPv6 listener address reaches MySQL connection (exit=%d, output=%s)",
	   ipv6.exit_code, ipv6.output.c_str());

	const char* const invalid_listeners[] = {
		"localhost:6020",
		"127.0.0.1",
		"127.0.0.1:",
		"127.0.0.1:0",
		"127.0.0.1:65536",
	};
	for (const char* listener : invalid_listeners) {
		CommandResult invalid = run_reader(cli.reader_bin, listener);
		ok(is_cli_error(invalid),
		   "invalid listener '%s' is a normal CLI error (exit=%d, output=%s)",
		   listener, invalid.exit_code, invalid.output.c_str());
	}

	const std::string overflow_port = "127.0.0.1:" + std::string(128, '9');
	CommandResult overflow = run_reader(cli.reader_bin, overflow_port);
	ok(is_cli_error(overflow),
	   "overflowing listener port is a normal CLI error (exit=%d, output=%s)",
	   overflow.exit_code, overflow.output.c_str());

	return exit_status();
}
