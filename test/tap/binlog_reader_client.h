#ifndef BINLOG_READER_TEST_CLIENT_H
#define BINLOG_READER_TEST_CLIENT_H

#include <string>
#include <vector>

#include "proxysql_gtid.h"

/**
 * One parsed line of the proxysql_binlog_reader on-wire protocol.
 *
 * The protocol is line-oriented; each line is one of:
 *
 *   ST=<uuid>:<interval>[:<interval>...]   initial GTID executed set
 *   I1=<uuid>:<trxid>                      new uuid + trxid
 *   I2=<trxid>                             same uuid as previous I1
 *   I3=<uuid>:<interval>                   batched: new uuid + interval
 *   I4=<interval>                          batched: same uuid as previous I3
 *
 * `intervals` holds one entry for I1/I2/I3/I4 and one-or-more for ST.
 * `uuid` is empty for I2/I4 (tests carry it over from the preceding
 * I1/I3).
 *
 * `error` is "" on success, otherwise one of the canonical error
 * strings exposed by binlog_reader_client.cpp.
 * `last_errno` is set only for socket-level failures.
 */
struct BinlogReaderMsg {
	std::string kind;
	std::string raw;
	std::string uuid;
	std::vector<TrxId_Interval> intervals;

	std::string error;
	int last_errno = 0;

	/** True if the read+parse succeeded (i.e. error is empty). */
	bool valid() const { return error.empty(); }
};

/**
 * Socket client for the proxysql_binlog_reader on-wire protocol.
 *
 * connect() establishes a TCP connection with a poll-based timeout.
 * read_line() returns one parsed BinlogReaderMsg per call; failure modes (timeout,
 * peer close, socket error, malformed line) are surfaced via the
 * returned BinlogReaderMsg's `error` / `last_errno` fields and `valid()`.
 */
class BinlogReaderClient {
   public:
	BinlogReaderClient() = default;
	~BinlogReaderClient();

	BinlogReaderClient(const BinlogReaderClient&) = delete;
	BinlogReaderClient& operator=(const BinlogReaderClient&) = delete;

	/**
	 * Establish a TCP connection to host:port.
	 *
	 * @param host       IPv4 address (no hostname resolution).
	 * @param port       TCP port.
	 * @param timeout_ms Poll deadline for the non-blocking connect.
	 *
	 * @return true on success; false on socket/connect/timeout failure.
	 */
	bool connect(const std::string& host, int port, int timeout_ms = 5000);

	/** Close the socket if open and clear the read buffer. Idempotent. */
	void disconnect();

	/** True if a connection is currently open. */
	bool connected() const { return fd_ >= 0; }

	/**
	 * Read one '\n'-terminated line from the wire and parse it.
	 *
	 * @param timeout_ms Deadline for the whole read (poll + recv).
	 *
	 * @return The parsed BinlogReaderMsg. On any failure msg.valid() is false and
	 *         msg.error names the cause; msg.last_errno carries the
	 *         system errno for socket-level failures.
	 */
	BinlogReaderMsg read_line(int timeout_ms = 5000);

   private:
	int fd_ = -1;
	std::string buf_;

	/**
	 * Block until more bytes are available, appending recv'd bytes to buf_.
	 *
	 * @param timeout_ms Poll deadline.
	 *
	 * @return true on a successful recv; false otherwise — errno conveys
	 *         the reason: ETIMEDOUT for poll timeout, 0 for clean peer
	 *         close, else the underlying poll/recv errno.
	 */
	bool fill_buffer(int timeout_ms);
};

#endif
