#!/usr/bin/env bash
# Run the binlog_reader TAP suite against one or more MySQL versions.
#
# Inputs (env):
#   MYSQL_VERSIONS    space-separated list of version tokens
#                     (default "57 80 84 90 94")
#   MYSQL_HOST        override host (default 127.0.0.1)
#   MYSQL_USER        override user (default root)
#   MYSQL_PASSWORD    override password (default root)
#   INFRA_CONTAINER   docker container to wait for before testing
#                     (default binlog-reader-infra; set empty to skip)
#   BINLOG_READER_LOG_DIR  when set, per-version reader logs go under
#                          ${BINLOG_READER_LOG_DIR}/mysql-${V}/reader.log
#                          and per-test TAP output is tee'd to
#                          ${BINLOG_READER_LOG_DIR}/mysql-${V}/${test}.log
#
# Per-version, the runner exports MYSQL_VERSION and MYSQL_PORT from the
# hardcoded table below, then executes every test/tap/tests/*-t binary
# in turn. Each test sees only the current version's target.

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

# ---------------------------------------------------------------------
# Inputs: load env-provided overrides with defaults that match the
# canonical docker infra (root/root, 127.0.0.1, the five MySQL tokens).
# ---------------------------------------------------------------------
MYSQL_VERSIONS=${MYSQL_VERSIONS:-"57 80 84 90 94"}
export MYSQL_HOST=${MYSQL_HOST:-127.0.0.1}
export MYSQL_USER=${MYSQL_USER:-root}
export MYSQL_PASSWORD=${MYSQL_PASSWORD:-root}

# Reader binary path. Empty means connect mode (test attaches to an
# externally-running reader). The "-" form preserves an explicit empty
# value passed by the caller; ":-" would replace it.
export BINLOG_READER_BIN=${BINLOG_READER_BIN-"$HERE/../../proxysql_binlog_reader"}

INFRA_CONTAINER=${INFRA_CONTAINER-binlog-reader-infra}

# ---------------------------------------------------------------------
# Version -> MySQL port table. Single source of truth shared with the
# infra entrypoint (test/infra/entrypoint.sh).
# ---------------------------------------------------------------------
port_for() {
  case "$1" in
    57) echo 3357 ;;
    80) echo 3380 ;;
    84) echo 3384 ;;
    90) echo 3390 ;;
    94) echo 3394 ;;
    *)  echo "unknown MYSQL_VERSION token: $1" >&2; return 1 ;;
  esac
}

# ---------------------------------------------------------------------
# Infra healthcheck gate.
#
# When a local docker-based infra container is running, block until its
# healthcheck reports healthy. No-ops cleanly when docker is unavailable
# or the named container doesn't exist — that mode is used when tests
# point at a remote / pre-existing MySQL.
# ---------------------------------------------------------------------
wait_for_infra() {
  [ -n "$INFRA_CONTAINER" ] || return 0
  command -v docker >/dev/null 2>&1 || return 0

  local state
  state=$(docker inspect -f '{{.State.Status}}' "$INFRA_CONTAINER" 2>/dev/null || true)
  [ -n "$state" ] || return 0

  echo "waiting for $INFRA_CONTAINER to be healthy..."
  local st
  for _ in $(seq 1 120); do
    st=$(docker inspect -f '{{.State.Health.Status}}' "$INFRA_CONTAINER" 2>/dev/null || echo missing)
    case "$st" in
      healthy) echo "$INFRA_CONTAINER healthy"; return 0 ;;
      missing) echo "$INFRA_CONTAINER not running"; return 1 ;;
      *)       sleep 2 ;;
    esac
  done
  echo "timed out waiting for $INFRA_CONTAINER (last state: $st)"
  return 1
}

wait_for_infra

# ---------------------------------------------------------------------
# Main loop.
#
# Outer loop: one MySQL version at a time. Each test binary sees a
# single target via the exported MYSQL_VERSION/MYSQL_PORT (and
# BINLOG_READER_LOG_FILE for the reader process). Per-version dir
# creation is gated on BINLOG_READER_LOG_DIR so host-mode runs (no
# log dir) don't need to deal with filesystem setup.
#
# overall_rc tracks failures across versions+tests so a later failing
# test doesn't get overwritten by a passing one.
# ---------------------------------------------------------------------
overall_rc=0
for v in $MYSQL_VERSIONS; do
  export MYSQL_VERSION="$v"
  export MYSQL_PORT="$(port_for "$v")"
  echo "### mysql=$v host=$MYSQL_HOST port=$MYSQL_PORT"

  # Per-version log dir setup. Reader process (in BinlogReaderProcess)
  # will dup2 its stdout/stderr onto BINLOG_READER_LOG_FILE.
  per_version_dir=""
  if [ -n "${BINLOG_READER_LOG_DIR:-}" ]; then
    per_version_dir="${BINLOG_READER_LOG_DIR}/mysql-${v}"
    mkdir -p "$per_version_dir"
    export BINLOG_READER_LOG_FILE="${per_version_dir}/reader.log"
  fi

  # Inner loop: every *-t binary in tests/. When a log dir is active,
  # each test's TAP output is tee'd to its own file. `set -o pipefail`
  # (from the shebang block) makes the pipe's exit code reflect the
  # test's, so failures still propagate to overall_rc.
  for t in tests/*-t; do
    [ -x "$t" ] || continue
    tname=$(basename "$t")
    echo "# $t"
    if [ -n "$per_version_dir" ]; then
      if ! "$t" 2>&1 | tee "${per_version_dir}/${tname}.log"; then
        overall_rc=1
      fi
    else
      if ! "$t"; then
        overall_rc=1
      fi
    fi
  done
done

exit "$overall_rc"
