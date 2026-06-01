#!/usr/bin/env bash
# Tear down the test infra. Pass --clean-logs to also delete
# test/logs/<RUN_ID>/ subdirectories.
#
# Log dirs may contain root-owned files (containers run as root),
# so cleanup goes through a short-lived alpine container.

set -euo pipefail

cd "$(dirname "$0")"
REPO_ROOT="$(cd ../.. && pwd)"

if [ "${1:-}" = "--clean-logs" ]; then
    echo "removing test/logs/*"
    docker run --rm \
        -v "${REPO_ROOT}/test/logs:/logs" \
        alpine \
        sh -c 'rm -rf /logs/[0-9]*Z'
fi

docker compose down -v --remove-orphans
