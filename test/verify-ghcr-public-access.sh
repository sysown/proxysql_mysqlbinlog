#!/usr/bin/env bash
# Verify that a GHCR image manifest can be read anonymously.
set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: test/verify-ghcr-public-access.sh ghcr.io/owner/image:tag" >&2
    exit 2
fi

reference=$1
if [[ ! "$reference" =~ ^ghcr\.io/([^/:]+)/([^/:]+):([^/:]+)$ ]]; then
    echo "image must be ghcr.io/owner/image:tag with a non-empty tag" >&2
    exit 2
fi

repository="${BASH_REMATCH[1]}/${BASH_REMATCH[2]}"
tag=${BASH_REMATCH[3]}
curl_bin=${CURL_BIN:-curl}
token_url=${GHCR_TOKEN_URL:-https://ghcr.io/token}
registry_url=${GHCR_REGISTRY_URL:-https://ghcr.io}

if ! token_response=$("$curl_bin" \
    --fail \
    --silent \
    --show-error \
    --get \
    --data-urlencode 'service=ghcr.io' \
    --data-urlencode "scope=repository:${repository}:pull" \
    "$token_url"); then
    echo "unable to request anonymous pull token" >&2
    exit 1
fi

if ! token=$(printf '%s' "$token_response" | python3 -c '
import json
import sys

try:
    response = json.load(sys.stdin)
except (json.JSONDecodeError, TypeError):
    sys.exit(1)

token = response.get("token") or response.get("access_token")
if not isinstance(token, str) or not token.strip():
    sys.exit(1)

sys.stdout.write(token)
'); then
    echo "anonymous pull token response did not contain a token" >&2
    exit 1
fi

if ! status=$("$curl_bin" \
    --silent \
    --show-error \
    --output /dev/null \
    --write-out '%{http_code}' \
    -H "Authorization: Bearer ${token}" \
    -H 'Accept: application/vnd.oci.image.index.v1+json, application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.docker.distribution.manifest.v2+json' \
    "${registry_url}/v2/${repository}/manifests/${tag}"); then
    status=000
fi

if [[ "$status" != 200 ]]; then
    echo "manifest request returned HTTP ${status}" >&2
    exit 1
fi
