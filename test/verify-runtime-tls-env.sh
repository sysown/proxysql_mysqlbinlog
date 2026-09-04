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
expected_source_label='org.opencontainers.image.source="https://github.com/sysown/proxysql_mysqlbinlog"'

source_label_assignments() {
    awk '
        function emit_source_label(token) {
            if (token ~ /^org\.opencontainers\.image\.source=/) {
                print token
            }
        }

        function inspect_label(instruction, text, position, character, token, quote, escaped) {
            sub(/^[[:space:]]*[Ll][Aa][Bb][Ee][Ll][[:space:]]+/, "", instruction)
            text = instruction
            token = ""
            quote = ""
            escaped = 0

            for (position = 1; position <= length(text); position++) {
                character = substr(text, position, 1)
                if (escaped) {
                    token = token character
                    escaped = 0
                } else if (character == "\\") {
                    token = token character
                    escaped = 1
                } else if (quote != "") {
                    token = token character
                    if (character == quote) {
                        quote = ""
                    }
                } else if (character == "\"" || character == "\047") {
                    token = token character
                    quote = character
                } else if (character ~ /[[:space:]]/) {
                    emit_source_label(token)
                    token = ""
                } else {
                    token = token character
                }
            }

            emit_source_label(token)
        }

        {
            line = $0
            sub(/[[:space:]]+$/, "", line)
            continued = line ~ /\\$/
            if (continued) {
                sub(/\\$/, "", line)
            }

            if (pending != "") {
                pending = pending " " line
                if (continued) {
                    next
                }
                if (pending ~ /^[[:space:]]*[Ll][Aa][Bb][Ee][Ll][[:space:]]/) {
                    inspect_label(pending)
                }
                pending = ""
                next
            }

            if (continued) {
                pending = line
            } else if (line ~ /^[[:space:]]*[Ll][Aa][Bb][Ee][Ll][[:space:]]/) {
                inspect_label(line)
            }
        }

        END {
            if (pending ~ /^[[:space:]]*[Ll][Aa][Bb][Ee][Ll][[:space:]]/) {
                inspect_label(pending)
            }
        }
    ' "$1"
}

verify_source_label_parser() {
    local fixture output
    fixture=$(mktemp)
    trap 'rm -f "$fixture"' RETURN
    printf '%s\n' \
        'FROM alpine' \
        'RUN printf "%s" \\' \
        'LABEL org.opencontainers.image.source="https://github.com/sysown/proxysql_mysqlbinlog"' \
        >"$fixture"
    output=$(source_label_assignments "$fixture")
    if [[ -n "$output" ]]; then
        echo 'continued non-LABEL instruction must not produce a source label' >&2
        fail=1
    fi
}

require_text() {
    local file=$1 command=$2 expected=$3
    if [[ "$command" != *"$expected"* ]]; then
        echo "$file: runtime CMD is missing $expected" >&2
        fail=1
    fi
}

verify_source_label_parser

for relative in "${dockerfiles[@]}"; do
    file="$repo/$relative"
    mapfile -t source_labels < <(source_label_assignments "$file")
    if [[ ${#source_labels[@]} -ne 1 ]] || [[ "${source_labels[0]:-}" != "$expected_source_label" ]]; then
        echo "$relative: expected exactly one org.opencontainers.image.source label with the expected value" >&2
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
