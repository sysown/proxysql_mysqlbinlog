#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: test/verify-package-dependencies.sh <package.deb|package.rpm>" >&2
    exit 2
fi

package_path=$1

case "${package_path}" in
    *.deb)
        dependencies="$(dpkg-deb -f "${package_path}" Depends)"
        ;;
    *.rpm)
        dependencies="$(rpm -qp --requires "${package_path}")"
        ;;
    *)
        echo "unsupported package type: ${package_path}" >&2
        exit 2
        ;;
esac

printf '%s\n' "${dependencies}"
if grep -Eqi 'libmariadb|mariadb-connector-c|libmysqlclient' <<<"${dependencies}"; then
    echo "package metadata must not depend on a MariaDB/MySQL client library" >&2
    exit 1
fi
