#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: test/verify-package-contents.sh <package.deb|package.rpm>" >&2
    exit 2
fi

package_path=$1
doc_dir=/usr/share/doc/proxysql-mysqlbinlog

expected_paths=(
    /bin/proxysql_binlog_reader
    "${doc_dir}/THIRD_PARTY_NOTICES.md"
    "${doc_dir}/COPYING.LIB"
)

case "${package_path}" in
    *.deb)
        package_paths="$(dpkg-deb --fsys-tarfile "${package_path}" | tar -tf - | sed 's#^\./#/#')"
        ;;
    *.rpm)
        package_paths="$(rpm -qlp "${package_path}")"
        ;;
    *)
        echo "unsupported package type: ${package_path}" >&2
        exit 2
        ;;
esac

for expected_path in "${expected_paths[@]}"; do
    if ! grep -Fxq "${expected_path}" <<<"${package_paths}"; then
        echo "package is missing ${expected_path}: ${package_path}" >&2
        exit 1
    fi
done
