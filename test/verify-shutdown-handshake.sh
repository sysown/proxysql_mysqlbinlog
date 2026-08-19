#!/usr/bin/env bash

set -euo pipefail

source_path=${1:-proxysql_binlog_reader.cpp}

if [[ ! -f "${source_path}" ]]; then
    echo "cannot read shutdown-handshake source: ${source_path}" >&2
    exit 2
fi

require_line() {
    local expected=$1
    if ! grep -Fq "${expected}" "${source_path}"; then
        echo "missing sequentially consistent shutdown operation: ${expected}" >&2
        exit 1
    fi
}

# If shutdown races event-loop startup, sequential consistency ensures that
# either the caller or the server thread observes the other side and sends the
# async break. Relaxed ordering permits both sides to miss that observation.
require_line 'stopflag.store(true, std::memory_order_seq_cst);'
require_line 'server_loop_ready.load(std::memory_order_seq_cst)'
require_line 'server_loop_ready.store(true, std::memory_order_seq_cst);'
require_line 'stopflag.load(std::memory_order_seq_cst)'
