#include "tls_options.h"

#include <algorithm>
#include <cctype>

namespace {

std::string uppercase(const std::string& value) {
	std::string result(value);
	std::transform(result.begin(), result.end(), result.begin(),
	               [](unsigned char character) {
			return static_cast<char>(std::toupper(character));
		});
	return result;
}

bool set_tls_option(MYSQL* mysql, enum mysql_option option, const void* value,
	                std::string* error) {
	if (mysql_options(mysql, option, value) == 0)
		return true;

	if (error)
		*error = mysql_error(mysql);
	return false;
}

const char* optional_value(const std::string& value) {
	return value.empty() ? nullptr : value.c_str();
}

bool has_tls_material(const TLSOptions& options) {
	return !options.ca_file.empty() || !options.ca_path.empty() ||
	       !options.certificate_file.empty() || !options.key_file.empty() ||
	       !options.cipher.empty() || !options.version.empty();
}

}  // namespace

bool parse_tls_mode(const std::string& value, TLSMode* result) {
	if (!result)
		return false;

	const std::string normalized = uppercase(value);
	if (normalized == "DISABLED") {
		*result = TLSMode::DISABLED;
	} else if (normalized == "PREFERRED") {
		*result = TLSMode::PREFERRED;
	} else if (normalized == "REQUIRED") {
		*result = TLSMode::REQUIRED;
	} else {
		return false;
	}

	return true;
}

bool parse_tls_boolean(const std::string& value, bool* result) {
	if (!result)
		return false;

	const std::string normalized = uppercase(value);
	if (normalized == "0" || normalized == "FALSE") {
		*result = false;
	} else if (normalized == "1" || normalized == "TRUE") {
		*result = true;
	} else {
		return false;
	}

	return true;
}

bool tls_options_valid(const TLSOptions& options, std::string* error) {
	if (options.mode != TLSMode::DISABLED) {
		if (error)
			error->clear();
		return true;
	}

	if (options.verify_server_certificate) {
		if (error)
			*error = "TLS certificate verification cannot be enabled when TLS is disabled";
		return false;
	}

	if (has_tls_material(options)) {
		if (error)
			*error = "TLS material cannot be configured when TLS is disabled";
		return false;
	}

	if (error)
		error->clear();
	return true;
}

bool apply_tls_options(MYSQL* mysql, const TLSOptions& options, std::string* error) {
	if (!mysql) {
		if (error)
			*error = "MYSQL handle is null";
		return false;
	}

	if (!tls_options_valid(options, error))
		return false;

	const my_bool enforce = options.mode == TLSMode::DISABLED ? 0 : 1;
	const my_bool verify = options.mode == TLSMode::DISABLED
	                           ? 0
	                           : (options.verify_server_certificate ? 1 : 0);

	if (!set_tls_option(mysql, MYSQL_OPT_SSL_ENFORCE, &enforce, error) ||
	    !set_tls_option(mysql, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify, error) ||
	    !set_tls_option(mysql, MYSQL_OPT_SSL_CA, optional_value(options.ca_file), error) ||
	    !set_tls_option(mysql, MYSQL_OPT_SSL_CAPATH, optional_value(options.ca_path), error) ||
	    !set_tls_option(mysql, MYSQL_OPT_SSL_CERT, optional_value(options.certificate_file), error) ||
	    !set_tls_option(mysql, MYSQL_OPT_SSL_KEY, optional_value(options.key_file), error) ||
	    !set_tls_option(mysql, MYSQL_OPT_SSL_CIPHER, optional_value(options.cipher), error) ||
	    !set_tls_option(mysql, MYSQL_OPT_TLS_VERSION, optional_value(options.version), error)) {
		return false;
	}

	if (error)
		error->clear();
	return true;
}

bool verify_tls_connection(MYSQL* mysql, const TLSOptions& options, std::string* error) {
	if (options.mode != TLSMode::REQUIRED ||
	    (mysql && mysql_get_ssl_cipher(mysql) != nullptr)) {
		if (error)
			error->clear();
		return true;
	}

	if (error)
		*error = "TLS is required but no TLS cipher was negotiated";
	return false;
}

const char* tls_mode_name(TLSMode mode) {
	switch (mode) {
	case TLSMode::DISABLED:
		return "DISABLED";
	case TLSMode::PREFERRED:
		return "PREFERRED";
	case TLSMode::REQUIRED:
		return "REQUIRED";
	}

	return "UNKNOWN";
}
