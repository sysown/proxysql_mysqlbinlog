#!/usr/bin/env bash
# Run the TAP suite against each built container image in CONNECT mode.
#
# For every distro the script stands up a FRESH MySQL (so no GTID state
# leaks between distros), runs the product image as an external reader in
# two groups, and points the test binaries at it:
#
#   batching group (-b 1 -t 300) -> the batched tests
#   normal   group (-b 0 -t 0)   -> the remaining tests
#
# Inputs (env, with defaults):
#   IMAGE_PREFIX   image repo to test          (proxysql/proxysql-mysqlbinlog)
#   DISTROS        space-separated distro tags  (all six)
#   MYSQL_VERSION  dbdeployer version token     (84); MYSQL_PORT is derived
#   RUNNER_IMG     image carrying the libs to run the *-t binaries
#
# Exit status is non-zero if any test failed.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
INFRA_DIR="$HERE"

IMAGE_PREFIX=${IMAGE_PREFIX:-proxysql/proxysql-mysqlbinlog}
DISTROS=${DISTROS:-"centos9 centos10 debian12 debian13 ubuntu22 ubuntu24"}
MYSQL_VERSION=${MYSQL_VERSION:-84}
RUNNER_IMG=${RUNNER_IMG:-proxysql/proxysql-mysqlbinlog:build-ubuntu24}
# Infra image (built by test/infra/docker-compose.yml); also provides the
# mysql client used below.
INFRA_IMG=${INFRA_IMG:-proxysql-binlog-reader-infra:latest}

INFRA_CONTAINER=binlog-reader-infra

# Version -> MySQL port table (mirrors test/tap/run.sh).
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
MYSQL_PORT=$(port_for "$MYSQL_VERSION") || exit 1

NORMAL_TESTS="test_basic_startup-t test_basic_updates-t test_connect_disconnect-t \
              test_consecutive_writes-t test_multi_client-t test_sparse_intervals-t"
BATCH_TESTS="test_batched_updates-t"

NET=""
rc=0

# Run one *-t binary in connect mode against the reader at IP $1.
run_test() { # $1=reader_ip  $2=test  $3=label
  local reader_ip=$1 test=$2 label=$3
  echo "::group::[$label] $test"
  if docker run --rm --network "$NET" \
       -v "$REPO":/opt/proxysql_mysqlbinlog -w /opt/proxysql_mysqlbinlog \
       -e MYSQL_HOST=mysql -e MYSQL_PORT="$MYSQL_PORT" -e MYSQL_USER=root \
       -e MYSQL_PASSWORD=root -e MYSQL_VERSION="$MYSQL_VERSION" \
       -e BINLOG_READER_BIN= -e BINLOG_READER_HOST="$reader_ip" -e BINLOG_READER_PORT=6020 \
       "$RUNNER_IMG" ./test/tap/tests/"$test"; then
    echo "RESULT $label/$test: PASS"
  else
    echo "RESULT $label/$test: FAIL"; rc=1
  fi
  echo "::endgroup::"
}

# Start the product image as a reader; echo its container IP on the infra net.
# The client connects by IPv4 only (inet_pton, no DNS), hence the IP.
start_reader() { # $1=image  $2=batching  $3=freq_ms
  local image=$1 batching=$2 freq_ms=$3
  docker rm -f reader >/dev/null 2>&1 || true
  docker run -d --name reader --network "$NET" \
    -e MYSQL_HOST=mysql -e MYSQL_PORT="$MYSQL_PORT" -e MYSQL_USER=root \
    -e MYSQL_PASSWORD=root -e BATCHING="$batching" -e UPDATE_FREQ_MS="$freq_ms" -e LISTEN_PORT=6020 \
    "$image" >/dev/null
  # give the reader time to connect and open its listen port
  sleep 4
  # Surface why the reader died (e.g. auth failures) — to stderr so it does
  # not pollute the IP captured by the caller.
  if [ "$(docker inspect -f '{{.State.Status}}' reader 2>/dev/null)" != running ]; then
    echo "reader container is not running; logs:" >&2
    docker logs reader >&2 2>&1 || true
  fi
  docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' reader
}

# Run one reader group: start the reader, run its tests, then drop it.
run_group() { # $1=image  $2=label  $3=batching  $4=freq_ms  $5=tests
  local image=$1 label=$2 batching=$3 freq_ms=$4 tests=$5
  local ip
  ip=$(start_reader "$image" "$batching" "$freq_ms")
  for t in $tests; do run_test "$ip" "$t" "$label"; done
  docker rm -f reader >/dev/null 2>&1 || true
}

# Recreate a clean MySQL and set NET to its docker network.
fresh_infra() {
  ( cd "$INFRA_DIR" && docker compose down -v >/dev/null 2>&1 || true )
  ( cd "$INFRA_DIR" && MYSQL_VERSIONS="$MYSQL_VERSION" docker compose up -d mysql )
  # wait up to ~5 min (60 x 5s) for the infra healthcheck to report healthy
  for _ in $(seq 1 60); do
    [ "$(docker inspect -f '{{.State.Health.Status}}' "$INFRA_CONTAINER" 2>/dev/null)" = healthy ] && break
    sleep 5
  done
  # let MySQL settle after it reports healthy
  sleep 5
  NET=$(docker inspect "$INFRA_CONTAINER" \
        --format '{{range $k,$v := .NetworkSettings.Networks}}{{$k}}{{end}}')
}

for distro in $DISTROS; do
  echo "================ DISTRO=$distro ================"
  img="${IMAGE_PREFIX}:${distro}"
  fresh_infra

  # Warm the caching_sha2 cache for root@'%' with a TLS client first: the
  # reader connects with SSL disabled, so it can only pass the warm fast-auth
  # path, never the cold handshake.
  docker run --rm --network "$NET" -e MYSQL_PORT="$MYSQL_PORT" \
    --entrypoint bash "$INFRA_IMG" \
    -c 'cli=$(ls /root/opt/mysql/8.4*/bin/mysql); "$cli" -h mysql -P "$MYSQL_PORT" -uroot -proot -e "SELECT 1"'

  # Batching group first (cleanest GTID state), then the normal group.
  run_group "$img" "$distro-batching" 1 300 "$BATCH_TESTS"
  run_group "$img" "$distro-normal"   0 0   "$NORMAL_TESTS"
done

( cd "$INFRA_DIR" && docker compose down -v >/dev/null 2>&1 || true )
echo "overall rc=$rc"
exit "$rc"
