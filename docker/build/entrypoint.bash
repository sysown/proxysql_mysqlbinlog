#!/bin/bash
#set -x

set -eu

echo "==> Build environment:"
env

ARCH=$(dpkg --print-architecture || rpm --eval '%{_arch}')
echo "==> '${ARCH}' architecture detected for package"

DIST=$(source /etc/os-release; echo ${ID%%[-._ ]*}${VERSION%%[-._ ]*})
echo "==> '${DIST}' distro detected for package"

echo -e "==> C compiler: ${CC} -> $(readlink -e $(which ${CC}))\n$(${CC} --version)"
echo -e "==> C++ compiler: ${CXX} -> $(readlink -e $(which ${CXX}))\n$(${CXX} --version)"
#echo -e "==> linker version:\n$ ${LD} -> $(readlink -e $(which ${LD}))\n$(${LD} --version)"

echo "==> Dependecies"
#git config --system --add safe.directory /opt/proxysql_mysqlbinlog
cd /opt/proxysql_mysqlbinlog

export SOURCE_DATE_EPOCH=$(git show -s --format=%ct HEAD)
echo "==> Epoch: ${SOURCE_DATE_EPOCH}"

echo "==> Cleaning"
make cleanbuild

echo "==> Normalizing mtimes for reproducible builds"
find . -not -path "./binaries/*" -not -path "./.git/*" | xargs touch -h --date=@${SOURCE_DATE_EPOCH}

echo "==> Building"
make -j $(ncpu)

make verify-static-connector-c

echo "==> Packaging"
cp -f ./proxysql_binlog_reader ./binaries/proxysql_binlog_reader-${GIT_VERS#v}-${PKG_DIST}

FPM_DEPENDS=()
case "${PKG_DIST}" in
	debian12|ubuntu22)
		FPM_DEPENDS+=(--depends libssl3)
		;;
	debian13|ubuntu24)
		FPM_DEPENDS+=(--depends libssl3t64)
		;;
	centos9|centos10)
		FPM_DEPENDS+=(--depends openssl-libs)
		;;
esac

fpm \
	--debug \
	-s dir \
	-t ${PKG_KIND} \
	--version ${PKG_VERS} \
	--source-date-epoch-default ${SOURCE_DATE_EPOCH} \
	--architecture native \
	--license GPLv2 \
	--category 'Development/Tools' \
	--rpm-summary 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks.' \
	--description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' \
	--url 'https://proxysql.com' \
	--vendor 'ProxySQL LLC' \
	--maintainer '<info@proxysql.com>' \
	"${FPM_DEPENDS[@]}" \
	--debug-workspace \
	--workdir /tmp/ \
	--package /opt/proxysql_mysqlbinlog/ \
	--name proxysql-mysqlbinlog \
	/opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/ \
	/opt/proxysql_mysqlbinlog/THIRD_PARTY_NOTICES.md=/usr/share/doc/proxysql-mysqlbinlog/ \
	/opt/proxysql_mysqlbinlog/mariadb-connector-c-3.4.8/COPYING.LIB=/usr/share/doc/proxysql-mysqlbinlog/


if [[ "${PKG_KIND}" = "deb" ]]; then
	mv -f ./proxysql-mysqlbinlog_${PKG_VERS}_${ARCH}.deb ./binaries/proxysql-mysqlbinlog_${PKG_VERS}-${PKG_DIST}_${ARCH}.deb
else
	mv -f ./proxysql-mysqlbinlog-${PKG_VERS}-1.${ARCH}.rpm ./binaries/proxysql-mysqlbinlog-${PKG_VERS}-1-${PKG_DIST}.${ARCH}.rpm
fi
ls -l binaries/
