#!/usr/bin/env bash
# Verify that production runtime images pass TLS configuration to the reader.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
dockerfiles=(
    docker/images/mysqlbinlog-centos9/Dockerfile
    docker/images/mysqlbinlog-centos10/Dockerfile
    docker/images/mysqlbinlog-debian12/Dockerfile
    docker/images/mysqlbinlog-debian13/Dockerfile
    docker/images/mysqlbinlog-ubuntu22/Dockerfile
    docker/images/mysqlbinlog-ubuntu24/Dockerfile
)

fail=0

require_text() {
    local file=$1 command=$2 expected=$3
    if [[ "$command" != *"$expected"* ]]; then
        echo "$file: runtime CMD is missing $expected" >&2
        fail=1
    fi
}

for relative in "${dockerfiles[@]}"; do
    file="$repo/$relative"
    mapfile -t source_labels < <(grep -Fx 'LABEL org.opencontainers.image.source="https://github.com/sysown/proxysql_mysqlbinlog"' "$file")
    if [[ ${#source_labels[@]} -ne 1 ]]; then
        echo "$relative: expected exactly one org.opencontainers.image.source label" >&2
        fail=1
    fi

    mapfile -t commands < <(awk '/^[[:space:]]*CMD[[:space:]]*\[/ { print }' "$file")
    if [[ ${#commands[@]} -ne 1 ]]; then
        echo "$relative: expected exactly one executable-form CMD" >&2
        fail=1
        continue
    fi

    command=${commands[0]}
    if [[ "$command" != 'CMD ["sh", "-ec", '* ]] ||
       [[ "$command" != *'set -- proxysql_binlog_reader '* ]] ||
       [[ "$command" != *'exec \"$@\"'* ]] ||
       [[ "$command" == *'eval '* ]]; then
        echo "$relative: runtime CMD must safely construct and exec reader argv" >&2
        fail=1
        continue
    fi

    require_text "$relative" "$command" '--ssl-mode \"${SSL_MODE:-REQUIRED}\"'
    require_text "$relative" "$command" '--ssl-verify-server-cert \"${SSL_VERIFY_SERVER_CERT:-1}\"'

    for setting in SSL_CA SSL_CAPATH SSL_CERT SSL_KEY SSL_CIPHER TLS_VERSION; do
        option="--$(tr '[:upper:]_' '[:lower:]-' <<<"$setting")"
        require_text "$relative" "$command" "[ -z \\\"\${$setting:-}\\\" ] || set -- \\\"\$@\\\" $option \\\"\$$setting\\\""
    done
done

exit "$fail"
