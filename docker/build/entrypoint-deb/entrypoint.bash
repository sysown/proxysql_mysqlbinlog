#!/bin/bash
#set -x

set -eu

echo "==> Build environment:"
env

ARCH=$(dpkg --print-architecture)
echo "==> '${ARCH}' architecture detected for package"

DIST=$(source /etc/os-release; echo ${ID%%[-._ ]*}${VERSION%%[-._ ]*})
echo "==> '${DIST}' distro detected for package"

echo -e "==> C compiler: ${CC} -> $(readlink -e $(which ${CC}))\n$(${CC} --version)"
echo -e "==> C++ compiler: ${CXX} -> $(readlink -e $(which ${CXX}))\n$(${CXX} --version)"
#echo -e "==> linker version:\n$ ${LD} -> $(readlink -e $(which ${LD}))\n$(${LD} --version)"

echo "==> Dependecies"
if [[ "$(cat /etc/os-release)" =~ "Debian GNU/Linux 9" ]]; then
	#echo "APT::Get::AllowUnauthenticated is 'true';" > /etc/apt/99mysql-cert-fail
	apt-get update
	apt-get -y purge libmariadbclient-dev
	#apt-get -y install libmysqld-dev
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient20_5.7.34-1debian9_amd64.deb
	dpkg -i libmysqlclient20_5.7.34-1debian9_amd64.deb
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient-dev_5.7.34-1debian9_amd64.deb
	dpkg -i libmysqlclient-dev_5.7.34-1debian9_amd64.deb
	apt-get -y install libboost-all-dev
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h
fi
if [[ "$(cat /etc/os-release)" =~ "Debian GNU/Linux 10" ]]; then
	#echo "APT::Get::AllowUnauthenticated is 'true';" > /etc/apt/99mysql-cert-fail
	apt-get update
	apt-get -y purge libmariadbclient-dev libmariadb-dev
	#apt-get -y install libmysqld-dev
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient20_5.7.37-1debian10_amd64.deb
	dpkg -i libmysqlclient20_5.7.37-1debian10_amd64.deb
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient-dev_5.7.37-1debian10_amd64.deb
	dpkg -i libmysqlclient-dev_5.7.37-1debian10_amd64.deb
#	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqld-dev_5.7.37-1debian10_amd64.deb
#	dpkg -i libmysqld-dev_5.7.37-1debian10_amd64.deb
	apt-get -y install libboost-all-dev
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h
fi

cd /opt/proxysql_mysqlbinlog

export SOURCE_DATE_EPOCH=$(git show -s --format=%ct HEAD)
echo "==> Epoch: ${SOURCE_DATE_EPOCH}"

echo "==> Cleaning"
make cleanbuild
#make cleanall

echo "==> Building"
make -j $(ncpu)

echo "==> Packaging"
cp -f ./proxysql_binlog_reader ./binaries/proxysql_binlog_reader-${GIT_VERS:1}-${IMG_NAME}
ls -l binaries/

#apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 467B942D3A79BD29
apt-get update

#apt-get -y install ruby rubygems ruby-dev
apt-get -y install ruby ruby-dev
gem install --no-ri --no-rdoc ffi -v '1.9.14'
#gem install --no-ri --no-rdoc git -v '1.6'
gem install --no-ri --no-rdoc fpm -v '1.11.0'
#gem install fpm

fpm \
	--debug \
	-s dir \
	-t deb \
	--version ${PKG_VERS} \
	--source-date-epoch-default ${SOURCE_DATE_EPOCH} \
	--architecture native \
	--license GPLv3 \
	--category 'Development/Tools' \
	--description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' \
	--url 'https://proxysql.com' \
	--vendor 'ProxySQL LLC' \
	--maintainer '<info@proxysql.com>' \
	--debug-workspace \
	--workdir /tmp/ \
	--package /opt/proxysql_mysqlbinlog/ \
	--name proxysql-mysqlbinlog \
	/opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/


mv -f ./proxysql-mysqlbinlog_${PKG_VERS}_${ARCH}.deb ./binaries/proxysql-mysqlbinlog_${GIT_VERS:1}-${IMG_NAME}_${ARCH}.deb
ls -l binaries/
