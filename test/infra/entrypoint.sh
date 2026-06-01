#!/usr/bin/env bash
# Deploy one standalone MySQL sandbox per token in MYSQL_VERSIONS, then
# wait for them all and stay alive.
#
# Each sandbox is configured for the proxysql_binlog_reader's needs:
#   gtid_mode=ON, enforce_gtid_consistency=ON, log-bin, binlog_format=ROW,
#   log-{slave,replica}-updates.
#
# After deploy we add root@'%' with password 'root' so tests can connect
# through Docker's port forwarding.

set -e
set -o pipefail

MYSQL_VERSIONS=${MYSQL_VERSIONS:-"57 80 84 90 94"}
ROOT_PASSWORD=${ROOT_PASSWORD:-root}

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

# Unique server-id per sandbox so log-bin is happy on 5.7.
server_id_for() {
    case "$1" in
        57) echo 157 ;;
        80) echo 180 ;;
        84) echo 184 ;;
        90) echo 190 ;;
        94) echo 194 ;;
    esac
}

# Resolve a token to the actual unpacked version directory under
# /root/opt/mysql/. Tarball patches may bump the patch level, so match
# by major.minor.
version_dir_for() {
    local token=$1
    local prefix
    case "$token" in
        57) prefix='5.7.' ;;
        80) prefix='8.0.' ;;
        84) prefix='8.4.' ;;
        90) prefix='9.0.' ;;
        94) prefix='9.4.' ;;
        *)  return 1 ;;
    esac
    ls /root/opt/mysql 2>/dev/null | grep "^${prefix}" | head -1
}

replica_updates_flag() {
    # `log-slave-updates` was renamed in 8.4.
    case "$1" in
        57|80) echo 'log-slave-updates' ;;
        *)     echo 'log-replica-updates' ;;
    esac
}

extra_auth_flags() {
    # 8.4 still ships mysql_native_password but disables it by default.
    # Re-enable so old replication clients (libslave-style) can still
    # authenticate. 9.x removed the plugin entirely.
    case "$1" in
        84) printf -- '-c\nmysql-native-password=ON\n' ;;
        *)  : ;;
    esac
}

mysql_client_for() {
    local dir
    dir=$(version_dir_for "$1")
    echo "/root/opt/mysql/${dir}/bin/mysql"
}

deploy_one() {
    local token=$1
    local port=$2
    local dir=$3
    local sandbox_name="msb_${dir//./_}"

    # Idempotent: container restart should re-create the sandbox cleanly.
    if dbdeployer sandboxes 2>/dev/null | grep -q "^${sandbox_name} "; then
        echo "==> removing existing sandbox ${sandbox_name}"
        dbdeployer delete "${sandbox_name}" >/dev/null
    fi

    echo "==> deploying MySQL ${dir} on port ${port}"
    # shellcheck disable=SC2046  # word-splitting needed for the optional pair
    dbdeployer deploy single "$dir" \
        --port="$port" \
        --bind-address=0.0.0.0 \
        -c "server-id=$(server_id_for "$token")" \
        -c log-bin=mysql-bin \
        -c gtid-mode=ON \
        -c enforce-gtid-consistency=ON \
        -c binlog-format=ROW \
        -c "$(replica_updates_flag "$token")=ON" \
        $(extra_auth_flags "$token")
}

configure_root() {
    local token=$1
    local port=$2
    local client
    client=$(mysql_client_for "$token")

    echo "==> configuring root@'%' on port ${port}"
    # dbdeployer's default root password is 'msandbox'.
    "$client" -h127.0.0.1 -P"$port" -uroot -pmsandbox <<SQL
ALTER USER 'root'@'localhost' IDENTIFIED BY '${ROOT_PASSWORD}';
CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED BY '${ROOT_PASSWORD}';
GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION;
GRANT REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO 'root'@'%';
FLUSH PRIVILEGES;
SQL
}

echo "========================================================================"
echo "binlog-reader infra: MYSQL_VERSIONS='${MYSQL_VERSIONS}'"
echo "========================================================================"

deployed=()
for v in $MYSQL_VERSIONS; do
    dir=$(version_dir_for "$v") || true
    if [ -z "$dir" ]; then
        echo "skip ${v}: no installed MySQL matches token under /root/opt/mysql"
        continue
    fi
    port=$(port_for "$v")
    deploy_one "$v" "$port" "$dir"
    configure_root "$v" "$port"
    deployed+=("$v:$port:$dir")
done

if [ "${#deployed[@]}" -eq 0 ]; then
    echo "no MySQL sandboxes deployed; staying alive for triage"
fi

echo "========================================================================"
echo "infra ready:"
for d in "${deployed[@]}"; do
    token=${d%%:*}
    rest=${d#*:}
    port=${rest%%:*}
    dir=${rest#*:}
    printf '  %-3s  port %-5s  (%s)\n' "$token" "$port" "$dir"
done
echo "========================================================================"

# Signal ready for the compose healthcheck.
touch /tmp/infra_ready

exec sleep infinity
