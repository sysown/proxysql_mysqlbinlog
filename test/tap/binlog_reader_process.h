#ifndef BINLOG_READER_TEST_PROCESS_H
#define BINLOG_READER_TEST_PROCESS_H

#include <sys/types.h>

#include <string>

/** Thin wrapper to start/stop proxysql_binlog_reader process. */
class BinlogReaderProcess {
   public:
	std::string binary;
	std::string mysql_host = "127.0.0.1";
	int         mysql_port = 3306;
	std::string mysql_user = "root";
	std::string mysql_password = "root";
	int         listen_port = 6020;
	std::string log_file_path;
	int         freq_ms = -1;
	int         batching = -1;
	long        max_netbuflen = -1;
	bool        foreground = true;

	BinlogReaderProcess() = default;
	~BinlogReaderProcess();

	BinlogReaderProcess(const BinlogReaderProcess&) = delete;
	BinlogReaderProcess& operator=(const BinlogReaderProcess&) = delete;

	/**
	 * Fork+exec the reader binary with the configured options.
	 *
	 * This does NOT wait for the listener to be ready; call wait_ready()
	 * for that. Child stdout/stderr inherit from the parent so test
	 * output captures the reader's logs.
	 *
	 * @return true once the child has been spawned; false on fork failure.
	 */
	bool start();

	/**
	 * Poll-connect to 127.0.0.1:reader_port until accept succeeds.
	 *
	 * @param timeout_ms Maximum time to wait for the listener.
	 *
	 * @return true when the listener accepts; false on timeout or
	 *         early child exit.
	 */
	bool wait_ready(int timeout_ms = 10000);

	/**
	 * Wait for the child to exit, with a deadline.
	 *
	 * @param timeout_ms   Maximum time to wait for the child to be reaped.
	 * @param exit_code    Out: WEXITSTATUS if exited normally, else -1.
	 * @param term_signal  Out: WTERMSIG if killed by a signal, else 0.
	 *
	 * @return true if the child was reaped within the deadline.
	 */
	bool wait_exit(int timeout_ms, int* exit_code = nullptr,
	               int* term_signal = nullptr);

	/**
	 * Terminate the child: SIGTERM first, then SIGKILL fallback.
	 *
	 * Idempotent — safe to call multiple times or when the child is
	 * already gone.
	 */
	void stop();

	/** The child's pid, or -1 if not running. */
	pid_t pid() const { return pid_; }

	/** True if a child has been spawned and not yet reaped. */
	bool running() const { return pid_ > 0; }

   private:
	pid_t pid_ = -1;
};

#endif
