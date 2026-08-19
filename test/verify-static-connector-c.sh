#!/usr/bin/env bash

set -euo pipefail

reader="${1:-./proxysql_binlog_reader}"

if [[ ! -f "$reader" ]]; then
    echo "reader binary is missing: $reader" >&2
    exit 1
fi

dynamic_libraries="$(ldd "$reader" 2>&1 || true)"
if grep -Eq 'lib(mariadb|mariadbclient|mysqlclient)' <<<"$dynamic_libraries"; then
    echo "reader dynamically links a MariaDB/MySQL client library:" >&2
    echo "$dynamic_libraries" >&2
    exit 1
fi

defined_symbols="$(nm -g --defined-only "$reader")"
if ! grep -q 'caching_sha2_password_client_plugin' <<<"$defined_symbols"; then
    echo "reader does not define caching_sha2_password_client_plugin" >&2
    exit 1
fi
