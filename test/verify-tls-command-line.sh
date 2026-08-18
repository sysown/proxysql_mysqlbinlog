#!/usr/bin/env bash

set -euo pipefail

reader="${1:-./proxysql_binlog_reader}"
workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

if [[ ! -x "$reader" ]]; then
    echo "reader binary is missing or is not executable: $reader" >&2
    exit 1
fi

run_failure() {
    local output="$1"
    shift

    if "$reader" "$@" >"$output" 2>&1; then
        echo "reader unexpectedly succeeded: $*" >&2
        exit 1
    fi
}

require_output() {
    local expected="$1"
    local output="$2"

    if ! grep -Fq "$expected" "$output"; then
        echo "expected output containing: $expected" >&2
        cat "$output" >&2
        exit 1
    fi
}

disabled_output="$workdir/disabled.out"
"$reader" -f -h 127.0.0.1 -P 1 -u root \
    --ssl-mode=DISABLED --ssl-verify-server-cert=0 >"$disabled_output" 2>&1 || true
require_output 'mysql_real_connect failed' "$disabled_output"
if grep -Fq 'unrecognized option' "$disabled_output"; then
    echo 'reader rejected supported TLS options' >&2
    exit 1
fi

invalid_mode_output="$workdir/invalid-mode.out"
run_failure "$invalid_mode_output" -f --ssl-mode=VERIFY_CA
require_output 'invalid SSL mode' "$invalid_mode_output"

invalid_combination_output="$workdir/invalid-combination.out"
run_failure "$invalid_combination_output" -f --ssl-mode=DISABLED --ssl-verify-server-cert=1
require_output 'TLS certificate verification cannot be enabled when TLS is disabled' \
    "$invalid_combination_output"
