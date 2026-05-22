#include "binlog_reader_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>

namespace {

const char* ERR_NOT_CONNECTED  = "not connected";
const char* ERR_TIMEOUT        = "timeout";
const char* ERR_CLOSED         = "connection closed by peer";
const char* ERR_SOCKET_ERROR   = "socket error";
const char* ERR_INVALID_FORMAT = "malformed line";

/**
 * Mark a file descriptor non-blocking.
 *
 * @param fd File descriptor to modify.
 *
 * @return 0 on success, -1 on fcntl failure.
 */
int set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * Issue a non-blocking connect() and wait for completion via poll.
 *
 * @param fd         A socket fd already created by socket(2).
 * @param addr       Resolved destination address.
 * @param timeout_ms Maximum time to wait for the connect to complete.
 *
 * @return true if the socket is connected; false on any failure.
 */
bool connect_with_timeout(int fd, const sockaddr_in& addr, int timeout_ms) {
	if (set_nonblock(fd) < 0)
		return false;

	int rc = connect(fd, reinterpret_cast<const sockaddr*>(&addr),
	                 sizeof(addr));
	if (rc == 0)
		return true;
	if (errno != EINPROGRESS)
		return false;

	pollfd pfd {};
	pfd.fd = fd;
	pfd.events = POLLOUT;

	rc = poll(&pfd, 1, timeout_ms);
	if (rc <= 0)
		return false;

	int err = 0;
	socklen_t err_len = sizeof(err);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0 || err != 0)
		return false;

	return true;
}

/**
 * Validate that a parsed interval is well-formed for the given message kind.
 *
 * Rejects empty/inverted ranges. Requires a degenerate (single-trxid)
 * interval for I1/I2.
 *
 * @param kind Message kind ("ST"/"I1"/"I2"/"I3"/"I4").
 * @param iv   Interval to check.
 *
 * @return true if iv is acceptable for the kind; false otherwise.
 */
bool valid_interval_for(const std::string& kind, const TrxId_Interval& iv) {
	if (iv.start < 1 || iv.end < iv.start)
		return false;
	if ((kind == "I1" || kind == "I2") && iv.start != iv.end)
		return false;
	return true;
}

/**
 * Parse the body of an I1/I2/I3/I4 line.
 *
 * Populates msg.uuid (for I1/I3) and appends one TrxId_Interval to
 * msg.intervals.
 *
 * @param msg In/out message; msg.kind and msg.raw are read, msg.uuid
 *            and msg.intervals are written.
 *
 * @return true on success; false if the body is malformed.
 */
bool parse_iv_line(BinlogReaderMsg& msg) {
	const std::string body = msg.raw.substr(3);
	const bool has_uuid = (msg.kind == "I1" || msg.kind == "I3");

	std::string interval_str;
	if (has_uuid) {
		const auto colon = body.find(':');
		if (colon == std::string::npos || colon == 0 || colon + 1 == body.size())
			return false;
		msg.uuid = body.substr(0, colon);
		interval_str = body.substr(colon + 1);
	} else {
		if (body.empty())
			return false;
		interval_str = body;
	}

	TrxId_Interval iv(interval_str);
	if (!valid_interval_for(msg.kind, iv))
		return false;
	msg.intervals.push_back(iv);
	return true;
}

/**
 * Parse the body of an ST line.
 *
 * The reader serializes a sparse same-uuid set with commas (streaming
 * mode) — e.g. "uuid:1-50,uuid:60-62,uuid:65-66" — and a non-sparse or
 * batched set with colons — e.g. "uuid:1-50:60-62". This handles both
 * by splitting on ',' first and then ':' within each block, requiring
 * all blocks to share the same uuid. True multi-uuid sets (different
 * uuids across blocks) are rejected — see integration-test-concerns.md
 * item #1.
 *
 * @param msg In/out message; msg.raw is read, msg.uuid + msg.intervals
 *            are written.
 *
 * @return true on success; false on malformed input or multi-uuid set.
 */
bool parse_st_line(BinlogReaderMsg& msg) {
	const std::string body = msg.raw.substr(3);
	if (body.empty())
		return false;

	size_t block_start = 0;
	while (block_start < body.size()) {
		const size_t comma = body.find(',', block_start);
		const std::string block = (comma == std::string::npos)
		                              ? body.substr(block_start)
		                              : body.substr(block_start, comma - block_start);
		if (block.empty())
			return false;

		const size_t first_colon = block.find(':');
		if (first_colon == std::string::npos || first_colon == 0 ||
		    first_colon + 1 == block.size())
			return false;
		const std::string block_uuid = block.substr(0, first_colon);
		if (msg.uuid.empty()) {
			msg.uuid = block_uuid;
		} else if (msg.uuid != block_uuid) {
			return false;
		}

		std::string rest = block.substr(first_colon + 1);
		size_t pos = 0;
		while (pos < rest.size()) {
			const size_t next = rest.find(':', pos);
			const std::string piece = (next == std::string::npos)
			                              ? rest.substr(pos)
			                              : rest.substr(pos, next - pos);
			if (piece.empty())
				return false;
			TrxId_Interval iv(piece);
			if (!valid_interval_for(msg.kind, iv))
				return false;
			msg.intervals.push_back(iv);
			if (next == std::string::npos)
				break;
			pos = next + 1;
		}

		if (comma == std::string::npos)
			break;
		block_start = comma + 1;
	}
	return !msg.intervals.empty();
}

}  // namespace

BinlogReaderClient::~BinlogReaderClient() {
	disconnect();
}

/**
 * Establish a TCP connection to host:port.
 *
 * @param host       IPv4 address (no hostname resolution).
 * @param port       TCP port.
 * @param timeout_ms Poll deadline for the non-blocking connect.
 *
 * @return true on success; false on socket/connect/timeout failure.
 */
bool BinlogReaderClient::connect(const std::string& host, int port,
                                 int timeout_ms) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
		close(fd);
		return false;
	}

	if (!connect_with_timeout(fd, addr, timeout_ms)) {
		close(fd);
		return false;
	}

	fd_ = fd;
	return true;
}

/** Close the socket if open and clear the read buffer. Idempotent. */
void BinlogReaderClient::disconnect() {
	if (fd_ >= 0) {
		close(fd_);
		fd_ = -1;
	}
	buf_.clear();
}

/**
 * Block until more bytes are available, appending recv'd bytes to buf_.
 *
 * @param timeout_ms Poll deadline.
 *
 * @return true on a successful recv; false otherwise — errno conveys
 *         the reason: ETIMEDOUT for poll timeout, 0 for clean peer
 *         close, else the underlying poll/recv errno.
 */
bool BinlogReaderClient::fill_buffer(int timeout_ms) {
	pollfd pfd {};
	pfd.fd = fd_;
	pfd.events = POLLIN;
	int rc = poll(&pfd, 1, timeout_ms);
	if (rc == 0) {
		errno = ETIMEDOUT;
		return false;
	}
	if (rc < 0)
		return false;

	char tmp[4096];
	ssize_t n = recv(fd_, tmp, sizeof(tmp), 0);
	if (n == 0) {
		errno = 0;
		return false;
	}
	if (n < 0)
		return false;
	buf_.append(tmp, static_cast<size_t>(n));
	return true;
}

/**
 * Read one '\n'-terminated line from the wire and parse it.
 *
 * @param timeout_ms Deadline for the whole read (poll + recv).
 *
 * @return The parsed BinlogReaderMsg. On any failure msg.valid() is false and
 *         msg.error names the cause; msg.last_errno carries the
 *         system errno for socket-level failures.
 */
BinlogReaderMsg BinlogReaderClient::read_line(int timeout_ms) {
	BinlogReaderMsg msg;

	if (fd_ < 0) {
		msg.error = ERR_NOT_CONNECTED;
		return msg;
	}

	const auto deadline = std::chrono::steady_clock::now() +
	                      std::chrono::milliseconds(timeout_ms);

	while (true) {
		const size_t nl = buf_.find('\n');
		if (nl != std::string::npos) {
			msg.raw = buf_.substr(0, nl);
			buf_.erase(0, nl + 1);
			break;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline) {
			msg.error = ERR_TIMEOUT;
			return msg;
		}
		const auto remaining =
		    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
		        .count();
		if (!fill_buffer(static_cast<int>(remaining))) {
			if (errno == 0) {
				msg.error = ERR_CLOSED;
			} else if (errno == ETIMEDOUT) {
				msg.error = ERR_TIMEOUT;
			} else {
				msg.error = ERR_SOCKET_ERROR;
				msg.last_errno = errno;
			}
			return msg;
		}
	}

	if (msg.raw.size() < 3 || msg.raw[2] != '=') {
		msg.error = ERR_INVALID_FORMAT;
		return msg;
	}
	const std::string head = msg.raw.substr(0, 2);
	if (head != "ST" && head != "I1" && head != "I2" && head != "I3" &&
	    head != "I4") {
		msg.error = ERR_INVALID_FORMAT;
		return msg;
	}
	msg.kind = head;

	const bool parsed = (msg.kind == "ST") ? parse_st_line(msg)
	                                       : parse_iv_line(msg);
	if (!parsed) {
		msg.kind.clear();
		msg.uuid.clear();
		msg.intervals.clear();
		msg.error = ERR_INVALID_FORMAT;
	}
	return msg;
}
