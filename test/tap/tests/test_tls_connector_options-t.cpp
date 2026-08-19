#include <string>

#include "tap.h"
#include "tls_options.h"

static MYSQL* new_mysql() {
	MYSQL* mysql = mysql_init(nullptr);
	if (!mysql)
		BAIL_OUT("mysql_init failed");
	return mysql;
}

static bool option_is(MYSQL* mysql, enum mysql_option option, bool expected) {
	my_bool value = 0;
	return mysql_get_optionv(mysql, option, &value) == 0 &&
	       value == (expected ? 1 : 0);
}

static bool option_is(MYSQL* mysql, enum mysql_option option,
	                  const std::string& expected) {
	char* value = nullptr;
	return mysql_get_optionv(mysql, option, &value) == 0 && value &&
	       expected == value;
}

int main() {
	plan(15);

	std::string error;

	MYSQL* required = new_mysql();
	TLSOptions required_options;
	ok(apply_tls_options(required, required_options, &error) &&
	       option_is(required, MYSQL_OPT_SSL_ENFORCE, true) &&
	       option_is(required, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, true),
	   "REQUIRED applies enforced TLS with server certificate verification");
	mysql_close(required);

	MYSQL* preferred = new_mysql();
	TLSOptions preferred_options;
	preferred_options.mode = TLSMode::PREFERRED;
	preferred_options.verify_server_certificate = false;
	ok(apply_tls_options(preferred, preferred_options, &error) &&
	       option_is(preferred, MYSQL_OPT_SSL_ENFORCE, true) &&
	       option_is(preferred, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, false),
	   "PREFERRED applies enforced TLS without server certificate verification");
	mysql_close(preferred);

	MYSQL* disabled = new_mysql();
	TLSOptions disabled_options;
	disabled_options.mode = TLSMode::DISABLED;
	disabled_options.verify_server_certificate = false;
	ok(apply_tls_options(disabled, disabled_options, &error) &&
	       option_is(disabled, MYSQL_OPT_SSL_ENFORCE, false) &&
	       option_is(disabled, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, false),
	   "DISABLED applies neither TLS enforcement nor server certificate verification");
	mysql_close(disabled);

	MYSQL* configured = new_mysql();
	TLSOptions configured_options;
	configured_options.ca_file = "test-ca.pem";
	configured_options.ca_path = "test-ca-directory";
	configured_options.certificate_file = "test-client-cert.pem";
	configured_options.key_file = "test-client-key.pem";
	configured_options.cipher = "ECDHE-RSA-AES256-GCM-SHA384";
	configured_options.version = "TLSv1.2";
	ok(apply_tls_options(configured, configured_options, &error),
	   "TLS material applies before connecting");
	ok(option_is(configured, MYSQL_OPT_SSL_CA, configured_options.ca_file),
	   "CA file survives Connector/C option mapping");
	ok(option_is(configured, MYSQL_OPT_SSL_CAPATH, configured_options.ca_path),
	   "CA path survives Connector/C option mapping");
	ok(option_is(configured, MYSQL_OPT_SSL_CERT, configured_options.certificate_file),
	   "client certificate survives Connector/C option mapping");
	ok(option_is(configured, MYSQL_OPT_SSL_KEY, configured_options.key_file),
	   "client key survives Connector/C option mapping");
	ok(option_is(configured, MYSQL_OPT_SSL_CIPHER, configured_options.cipher),
	   "TLS cipher survives Connector/C option mapping");
	ok(option_is(configured, MYSQL_OPT_TLS_VERSION, configured_options.version),
	   "TLS version survives Connector/C option mapping");
	mysql_close(configured);

	MYSQL* unconnected = new_mysql();
	error.clear();
	ok(!verify_tls_connection(unconnected, required_options, &error) &&
	       error == "TLS is required but no TLS cipher was negotiated",
	   "REQUIRED rejects an unconnected MYSQL handle without a TLS cipher");
	ok(verify_tls_connection(unconnected, preferred_options, &error),
	   "PREFERRED accepts an unconnected MYSQL handle without a TLS cipher");
	mysql_close(unconnected);

	ok(std::string(tls_mode_name(TLSMode::DISABLED)) == "DISABLED",
	   "DISABLED TLS mode name is stable");
	ok(std::string(tls_mode_name(TLSMode::PREFERRED)) == "PREFERRED",
	   "PREFERRED TLS mode name is stable");
	ok(std::string(tls_mode_name(TLSMode::REQUIRED)) == "REQUIRED",
	   "REQUIRED TLS mode name is stable");

	return exit_status();
}
