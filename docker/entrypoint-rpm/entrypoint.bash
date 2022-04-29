#!/bin/bash
#set -x

set -eu

echo "==> Build environment:"
env

ARCH=$(rpm --eval '%{_arch}')
echo "==> '${ARCH}' architecture detected for package"

DIST=$(source /etc/os-release; echo ${ID%%[-._ ]*}${VERSION%%[-._ ]*})
echo "==> '${DIST}' distro detected for package"

echo -e "==> C compiler: ${CC} -> $(readlink -e $(which ${CC}))\n$(${CC} --version)"
echo -e "==> C++ compiler: ${CXX} -> $(readlink -e $(which ${CXX}))\n$(${CXX} --version)"
#echo -e "==> linker version:\n$ ${LD} -> $(readlink -e $(which ${LD}))\n$(${LD} --version)"

echo "==> Dependecies"
if [[ "$(cat /etc/os-release)" =~ "CentOS Linux 8" ]]; then
	yum update -y
	yum install -y nss libtool boost boost-devel
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-devel-5.7.28-1.el7.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-libs-5.7.28-1.el7.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-common-5.7.28-1.el7.x86_64.rpm
	rpm -ihv mysql-community-common-5.7.28-1.el7.x86_64.rpm mysql-community-devel-5.7.28-1.el7.x86_64.rpm mysql-community-libs-5.7.28-1.el7.x86_64.rpm
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h
fi
if [[ "$(cat /etc/os-release)" =~ "CentOS Linux 7" ]]; then
	yum update -y
	yum install -y nss curl libcurl libtool boost boost-devel
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-devel-5.7.28-1.el7.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-libs-5.7.28-1.el7.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-common-5.7.28-1.el7.x86_64.rpm
	rpm -ihv mysql-community-common-5.7.28-1.el7.x86_64.rpm mysql-community-devel-5.7.28-1.el7.x86_64.rpm mysql-community-libs-5.7.28-1.el7.x86_64.rpm
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h
fi
if [[ "$(cat /etc/redhat-release)" =~ "CentOS release 6" ]]; then
	yum update -y
	yum install -y nss curl libcurl libtool boost boost-devel
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/6/x86_64/mysql-community-client-5.7.37-1.el6.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/6/x86_64/mysql-community-libs-5.7.37-1.el6.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/6/x86_64/mysql-community-common-5.7.37-1.el6.x86_64.rpm
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/6/x86_64/mysql-community-devel-5.7.37-1.el6.x86_64.rpm
	rpm -ihv mysql-community-client-5.7.37-1.el6.x86_64.rpm mysql-community-libs-5.7.37-1.el6.x86_64.rpm mysql-community-common-5.7.37-1.el6.x86_64.rpm mysql-community-devel-5.7.37-1.el6.x86_64.rpm
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h
fi
cd /opt/proxysql_mysqlbinlog

export SOURCE_DATE_EPOCH=$(git show -s --format=%ct HEAD)

echo "==> Cleaning"
make cleanbuild
#make cleanall

echo "==> Building"
make -j $(ncpu)

echo "==> Packaging"
cp -f ./proxysql_binlog_reader ./binaries/proxysql_binlog_reader-${GIT_VERS:1}-${IMG_NAME}
ls -l binaries/

yum -y install ruby rubygems ruby-devel
#gem install --version 1.12.2 --user-install ffi
#gem install --version 1.6.0 --user-install git
#gem install fpm
gem install --no-ri --no-rdoc ffi -v '1.9.14'
gem install --no-ri --no-rdoc git -v '1.6'
gem install --no-ri --no-rdoc fpm -v '1.11.0'

fpm \
	-s dir \
	-t rpm \
	--version ${PKG_VERS} \
	--license GPLv3 \
	--category 'Development/Tools' \
	--rpm-summary 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks.' \
	--description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' \
	--url 'https://proxysql.com' \
	--vendor 'ProxySQL LLC' \
	--debug-workspace \
	--workdir /tmp/ \
	--package=/opt/proxysql_mysqlbinlog/ \
	--name proxysql-mysqlbinlog \
	/opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/


mv -f ./proxysql-mysqlbinlog-${PKG_VERS}-1.${ARCH}.rpm ./binaries/proxysql-mysqlbinlog-${GIT_VERS:1}-${IMG_NAME}.${ARCH}.rpm
ls -l binaries/
