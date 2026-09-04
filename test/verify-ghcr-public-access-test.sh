#!/usr/bin/env bash
# Unit tests for the anonymous GHCR manifest-access verifier.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
verifier="$repo/test/verify-ghcr-public-access.sh"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

fake_curl="$tmpdir/curl"
call_log="$tmpdir/calls"

fail() {
    echo "verify-ghcr-public-access-test: $*" >&2
    exit 1
}

cat >"$fake_curl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

has_arg() {
    local expected=$1 arg
    shift
    for arg in "$@"; do
        [[ "$arg" == "$expected" ]] && return 0
    done
    return 1
}

has_header() {
    local expected=$1 previous= arg
    shift
    for arg in "$@"; do
        if [[ "$previous" == -H && "$arg" == "$expected" ]]; then
            return 0
        fi
        previous=$arg
    done
    return 1
}

has_data_urlencode() {
    local expected=$1 previous= arg
    shift
    for arg in "$@"; do
        if [[ "$previous" == --data-urlencode && "$arg" == "$expected" ]]; then
            return 0
        fi
        previous=$arg
    done
    return 1
}

url=${!#}
if [[ "$url" == "$GHCR_TOKEN_URL" ]]; then
    has_arg --fail "$@" || exit 64
    has_arg --silent "$@" || exit 64
    has_arg --show-error "$@" || exit 64
    has_arg --get "$@" || exit 64
    has_data_urlencode 'service=ghcr.io' "$@" || exit 64
    has_data_urlencode 'scope=repository:acme/widget:pull' "$@" || exit 64
    printf 'token\n' >>"$FAKE_CURL_CALL_LOG"
    if [[ "${FAKE_CURL_MODE:-success}" == token_failure ]]; then
        exit 22
    fi
    printf '%s\n' '{"access_token":"anonymous-token"}'
    exit 0
fi

[[ "$url" == "$GHCR_REGISTRY_URL/v2/acme/widget/manifests/latest" ]] || exit 64
[[ "$(head -n 1 "$FAKE_CURL_CALL_LOG")" == token ]] || exit 64
has_arg --silent "$@" || exit 64
has_arg --show-error "$@" || exit 64
has_arg --output /dev/null "$@" || exit 64
has_arg --write-out '%{http_code}' "$@" || exit 64
has_header 'Authorization: Bearer anonymous-token' "$@" || exit 64
has_header 'Accept: application/vnd.oci.image.index.v1+json, application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.docker.distribution.manifest.v2+json' "$@" || exit 64
printf 'manifest\n' >>"$FAKE_CURL_CALL_LOG"
if [[ "${FAKE_CURL_MODE:-success}" == manifest_failure ]]; then
    printf '401'
else
    printf '200'
fi
EOF
chmod +x "$fake_curl"

run_verifier() {
    local mode=$1
    FAKE_CURL_MODE="$mode" \
        FAKE_CURL_CALL_LOG="$call_log" \
        CURL_BIN="$fake_curl" \
        GHCR_TOKEN_URL='https://token.test/token' \
        GHCR_REGISTRY_URL='https://registry.test' \
        "$verifier" ghcr.io/acme/widget:latest
}

assert_status() {
    local expected=$1 actual=$2 name=$3
    [[ "$actual" -eq "$expected" ]] || fail "$name: expected exit $expected, got $actual"
}

set +e
run_verifier success >/dev/null 2>&1
success_status=$?
set -e
assert_status 0 "$success_status" success

: >"$call_log"
set +e
token_output=$(run_verifier token_failure 2>&1)
token_status=$?
set -e
assert_status 1 "$token_status" token_failure
[[ "$token_output" == *'unable to request anonymous pull token'* ]] || fail 'token_failure: missing token diagnostic'

: >"$call_log"
set +e
manifest_output=$(run_verifier manifest_failure 2>&1)
manifest_status=$?
set -e
assert_status 1 "$manifest_status" manifest_failure
[[ "$manifest_output" == *'manifest request returned HTTP 401'* ]] || fail 'manifest_failure: missing manifest diagnostic'
