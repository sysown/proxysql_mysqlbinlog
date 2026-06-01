#include "binlog_reader_process.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

#include "tap.h"

namespace {

/**
 * Probe whether a TCP listener is accepting on host:port.
 *
 * @param host IPv4 address to connect to.
 * @param port TCP port.
 *
 * @return true if connect() succeeds; false otherwise.
 */
bool port_open(const std::string& host, int port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

	int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	close(fd);
	return rc == 0;
}

}  // namespace

BinlogReaderProcess::~BinlogReaderProcess() {
	stop();
}

/**
 * Fork+exec the reader binary with the configured options.
 *
 * This does NOT wait for the listener to be ready; call wait_ready()
 * for that. Child stdout/stderr inherit from the parent so test output
 * captures the reader's logs.
 *
 * @return true once the child has been spawned; false on fork failure.
 */
bool BinlogReaderProcess::start() {
	if (pid_ > 0) {
		diag("BinlogReaderProcess::start called while already running (pid=%d)",
		     pid_);
		return false;
	}

	std::vector<std::string> argv;

	argv.push_back(binary);
	
	argv.push_back("-h");
	argv.push_back(mysql_host);

	argv.push_back("-P");
	argv.push_back(std::to_string(mysql_port));

	argv.push_back("-u");
	argv.push_back(mysql_user);

	if (!mysql_password.empty()) {
		argv.push_back("-p");
		argv.push_back(mysql_password);
	}

	argv.push_back("-l");
	argv.push_back(std::to_string(listen_port));

	if (freq_ms >= 0) {
		argv.push_back("-t");
		argv.push_back(std::to_string(freq_ms));
	}

	if (batching >= 0) {
		argv.push_back("-b");
		argv.push_back(std::to_string(batching));
	}

	if (max_netbuflen >= 0) {
		argv.push_back("-B");
		argv.push_back(std::to_string(max_netbuflen));
	}

	if (foreground)
		argv.push_back("-f");

	std::vector<char*> c_argv(argv.size() + 1, nullptr);
	for (size_t i = 0; i < argv.size(); ++i)
		c_argv[i] = const_cast<char*>(argv[i].c_str());

	pid_t child = fork();
	if (child < 0) {
		diag("fork() failed: %s", strerror(errno));
		return false;
	}
	if (child == 0) {
		if (!log_file_path.empty()) {
			int fd = open(log_file_path.c_str(),
			              O_WRONLY | O_APPEND | O_CREAT, 0644);
			if (fd >= 0) {
				dup2(fd, STDOUT_FILENO);
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
		}
		execv(binary.c_str(), c_argv.data());
		fprintf(stderr, "execv(%s) failed: %s\n", binary.c_str(),
		        strerror(errno));
		_exit(127);
	}

	pid_ = child;
	return true;
}

/**
 * Poll-connect to 127.0.0.1:reader_port until accept succeeds.
 *
 * Also reaps the child via waitpid(WNOHANG) so an early exit surfaces
 * immediately as a failure (rather than burning the timeout).
 *
 * @param timeout_ms Maximum time to wait for the listener.
 *
 * @return true when the listener accepts; false on timeout or early
 *         child exit.
 */
bool BinlogReaderProcess::wait_ready(int timeout_ms) {
	const auto deadline = std::chrono::steady_clock::now() +
	                      std::chrono::milliseconds(timeout_ms);
	while (std::chrono::steady_clock::now() < deadline) {
		int status = 0;
		pid_t r = waitpid(pid_, &status, WNOHANG);
		if (r == pid_) {
			diag("reader exited during startup (status=0x%x)", status);
			pid_ = -1;
			return false;
		}
		if (port_open("127.0.0.1", listen_port))
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return false;
}

/**
 * Wait for the child to exit, with a deadline.
 *
 * @param timeout_ms   Maximum time to wait for the child to be reaped.
 * @param exit_code    Out: WEXITSTATUS if exited normally, else -1.
 * @param term_signal  Out: WTERMSIG if killed by a signal, else 0.
 *
 * @return true if the child was reaped within the deadline.
 */
bool BinlogReaderProcess::wait_exit(int timeout_ms, int* exit_code,
                                    int* term_signal) {
	if (pid_ <= 0)
		return false;

	const auto deadline = std::chrono::steady_clock::now() +
	                      std::chrono::milliseconds(timeout_ms);
	int status = 0;
	while (std::chrono::steady_clock::now() < deadline) {
		pid_t r = waitpid(pid_, &status, WNOHANG);
		if (r == pid_) {
			if (exit_code)
				*exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
			if (term_signal)
				*term_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
			pid_ = -1;
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return false;
}

/**
 * Terminate the child: SIGTERM first, then SIGKILL fallback.
 *
 * Idempotent — safe to call multiple times or when the child is
 * already gone.
 */
void BinlogReaderProcess::stop() {
	if (pid_ <= 0)
		return;
	kill(pid_, SIGTERM);
	if (wait_exit(2000))
		return;
	kill(pid_, SIGKILL);
	wait_exit(2000);
}
