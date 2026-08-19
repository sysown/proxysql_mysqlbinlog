#include "mariadb_replication.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {

bool is_hex(char c) {
	return std::isdigit(static_cast<unsigned char>(c)) ||
	       (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool is_separator_whitespace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim_separator_whitespace(const std::string& input) {
	size_t start = 0;
	while (start < input.size() && is_separator_whitespace(input[start]))
		++start;

	size_t end = input.size();
	while (end > start && is_separator_whitespace(input[end - 1]))
		--end;

	return input.substr(start, end - start);
}

bool normalize_uuid(const std::string& input, std::string* output) {
	if (!output || (input.size() != 32 && input.size() != 36))
		return false;

	std::string normalized;
	normalized.reserve(32);
	for (size_t i = 0; i < input.size(); ++i) {
		const char c = input[i];
		const bool dash_position = i == 8 || i == 13 || i == 18 || i == 23;
		if (input.size() == 36 && dash_position) {
			if (c != '-')
				return false;
			continue;
		}
		if (!is_hex(c))
			return false;
		normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	if (normalized.size() != 32)
		return false;
	*output = normalized;
	return true;
}

bool parse_positive_trxid(const std::string& input, trxid_t* out) {
	if (!out || input.empty())
		return false;

	uint64_t value = 0;
	for (char c : input) {
		if (!std::isdigit(static_cast<unsigned char>(c)))
			return false;
		const uint64_t digit = static_cast<uint64_t>(c - '0');
		if (value > (static_cast<uint64_t>(std::numeric_limits<trxid_t>::max()) - digit) / 10)
			return false;
		value = value * 10 + digit;
	}

	if (value == 0)
		return false;
	*out = static_cast<trxid_t>(value);
	return true;
}

bool add_interval(const std::string& input, const std::string& uuid, GTID_Set* set) {
	const size_t dash = input.find('-');
	if (dash == std::string::npos) {
		trxid_t trxid = 0;
		return parse_positive_trxid(input, &trxid) && set->add(uuid, trxid);
	}
	if (dash == 0 || dash == input.size() - 1 || input.find('-', dash + 1) != std::string::npos)
		return false;

	trxid_t start = 0;
	trxid_t end = 0;
	if (!parse_positive_trxid(input.substr(0, dash), &start) ||
	    !parse_positive_trxid(input.substr(dash + 1), &end) || start > end)
		return false;
	set->add(uuid, start, end);
	return true;
}

}  // namespace

std::string mysql_uuid_from_bytes(const unsigned char* source_id) {
	if (!source_id)
		return "";

	static const char hex[] = "0123456789abcdef";
	std::string uuid;
	uuid.reserve(32);
	for (size_t i = 0; i < 16; ++i) {
		uuid += hex[(source_id[i] >> 4) & 0x0f];
		uuid += hex[source_id[i] & 0x0f];
	}
	return uuid;
}

bool parse_mysql_gtid_executed(const std::string& encoded, GTID_Set* out) {
	if (!out)
		return false;

	GTID_Set parsed;
	if (encoded.empty()) {
		*out = parsed;
		return true;
	}

	size_t set_start = 0;
	while (set_start < encoded.size()) {
		const size_t set_end = encoded.find(',', set_start);
		const std::string entry = trim_separator_whitespace(encoded.substr(
		    set_start, set_end == std::string::npos ? std::string::npos : set_end - set_start));
		const size_t uuid_end = entry.find(':');
		if (uuid_end == std::string::npos || uuid_end == 0 || uuid_end == entry.size() - 1)
			return false;

		std::string uuid;
		if (!normalize_uuid(entry.substr(0, uuid_end), &uuid))
			return false;

		size_t interval_start = uuid_end + 1;
		while (interval_start < entry.size()) {
			const size_t interval_end = entry.find(':', interval_start);
			const std::string interval = entry.substr(
			    interval_start, interval_end == std::string::npos
			                        ? std::string::npos
			                        : interval_end - interval_start);
			if (!add_interval(interval, uuid, &parsed))
				return false;
			if (interval_end == std::string::npos)
				break;
			interval_start = interval_end + 1;
		}

		if (set_end == std::string::npos)
			break;
		set_start = set_end + 1;
		if (set_start == encoded.size())
			return false;
	}

	*out = parsed;
	return true;
}

bool parse_mysql_snapshot_position(const char* filename, const char* encoded_position,
                                   unsigned long* out) {
	if (!filename || !*filename || !encoded_position || !*encoded_position || !out)
		return false;

	for (const char* cursor = encoded_position; *cursor; ++cursor) {
		if (*cursor < '0' || *cursor > '9')
			return false;
	}

	errno = 0;
	char* end = nullptr;
	const unsigned long position = std::strtoul(encoded_position, &end, 10);
	if (errno == ERANGE || !end || *end != '\0' || position < 4)
		return false;

	*out = position;
	return true;
}
