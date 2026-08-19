#include <string>

#include "tap.h"
#include "tls_options.h"

int main() {
	plan(12);

	TLSOptions defaults;
	ok(defaults.mode == TLSMode::REQUIRED, "default TLS mode is REQUIRED");
	ok(defaults.verify_server_certificate, "server certificate verification defaults to true");

	TLSMode mode = TLSMode::REQUIRED;
	ok(parse_tls_mode("DISABLED", &mode) && mode == TLSMode::DISABLED,
	   "DISABLED TLS mode parses");
	ok(parse_tls_mode("preferred", &mode) && mode == TLSMode::PREFERRED,
	   "lowercase preferred TLS mode parses");
	ok(parse_tls_mode("REQUIRED", &mode) && mode == TLSMode::REQUIRED,
	   "REQUIRED TLS mode parses");
	ok(!parse_tls_mode("VERIFY_CA", &mode), "VERIFY_CA TLS mode is rejected");

	bool value = true;
	ok(parse_tls_boolean("0", &value) && !value, "zero TLS boolean parses as false");
	ok(parse_tls_boolean("true", &value) && value, "true TLS boolean parses as true");
	ok(!parse_tls_boolean("yes", &value), "yes TLS boolean is rejected");

	std::string error;
	ok(tls_options_valid(defaults, &error), "default TLS options validate");

	TLSOptions disabled;
	disabled.mode = TLSMode::DISABLED;
	ok(!tls_options_valid(disabled, &error),
	   "DISABLED TLS mode with certificate verification is invalid");
	disabled.verify_server_certificate = false;
	ok(tls_options_valid(disabled, &error),
	   "DISABLED TLS mode without certificate verification validates");

	return exit_status();
}
