#!/usr/bin/env bash
# Run the binlog_reader TAP suite inside the runner container.
#
# Prereqs (built on the host):
#   - proxysql_binlog_reader (run `make build-ubuntu24` at repo root)
#   - test/tap/libtap.a + test/tap/tests/*-t (run `make -C test/tap`)
#
# Flow:
#   1. Generate a fresh RUN_ID (timestamp).
#   2. Create test/logs/${RUN_ID}/.
#   3. Sanity-check that the host artifacts exist.
#   4. Bring up the infra (idempotent) and run the runner container with
#      its output streamed to the terminal.
#
# The runner inherits RUN_ID via the compose environment, writes its
# logs into test/logs/${RUN_ID}/, and exits with the TAP suite's exit
# code. Infra is left running for post-mortem; use clean-infra.sh to
# tear down.

set -euo pipefail

cd "$(dirname "$0")"
REPO_ROOT="$(cd ../.. && pwd)"

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
export RUN_ID

LOG_DIR="${REPO_ROOT}/test/logs/${RUN_ID}"
mkdir -p "${LOG_DIR}"
echo "logs: ${LOG_DIR}"

if [ ! -x "${REPO_ROOT}/proxysql_binlog_reader" ]; then
    echo "error: ${REPO_ROOT}/proxysql_binlog_reader not found"
    echo "       run 'make build-ubuntu24' at the repo root first"
    exit 1
fi
if [ -z "$(ls "${REPO_ROOT}"/test/tap/tests/*-t 2>/dev/null)" ]; then
    echo "error: no compiled tests under test/tap/tests/"
    echo "       run 'make -C test/tap' first"
    exit 1
fi

docker compose up -d mysql
docker compose run --rm runner
