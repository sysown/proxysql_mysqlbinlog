.PHONY: default
default: proxysql_binlog_reader


centos7: binaries/proxysql_binlog_reader-centos7
.PHONY: centos7

ubuntu18: binaries/proxysql_binlog_reader-ubuntu18
.PHONY: ubuntu18

ubuntu16: binaries/proxysql_binlog_reader-ubuntu16
.PHONY: ubuntu16

ubuntu14: binaries/proxysql_binlog_reader-ubuntu14
.PHONY: ubuntu14

debian9: binaries/proxysql_binlog_reader-debian9
.PHONY: debian9

debian8: binaries/proxysql_binlog_reader-debian8
.PHONY: debian8

debian7: binaries/proxysql_binlog_reader-debian7
.PHONY: debian7


proxysql_binlog_reader: proxysql_binlog_reader.cpp libev/.libs/libev.a libdaemon/libdaemon/.libs/libdaemon.a
	g++ -o proxysql_binlog_reader proxysql_binlog_reader.cpp -std=c++11 -ggdb ./libslave/libslave.a ./libev/.libs/libev.a libdaemon/libdaemon/.libs/libdaemon.a -I./libslave/ -I./libev/ -I./libdaemon/ -L/usr/lib64/mysql -rdynamic -lz -ldl -lssl -lcrypto -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lssl -lcrypto -pthread
# -lperconaserverclient if compiled with percona server

libev/.libs/libev.a:
	rm -rf libev-4.24 || true
	tar -zxf libev-4.24.tar.gz
	cd libev-4.24 && ./configure
	cd libev && CC=${CC} CXX=${CXX} ${MAKE}

libdaemon/libdaemon/.libs/libdaemon.a: 
	rm -rf libdaemon-0.14 || true
	tar -zxf libdaemon-0.14.tar.gz
	cd libdaemon && ./configure --disable-examples
	cd libdaemon && CC=${CC} CXX=${CXX} ${MAKE}

binaries/proxysql_binlog_reader-centos7:
	docker stop centos7_build || true
	docker rm centos7_build || true
	docker create --name centos7_build renecannao/proxysql:build-centos7 bash -c "while : ; do sleep 10 ; done"
	docker start centos7_build
	docker exec centos7_build bash -c "yum install -y nss curl libcurl libtool boost boost-devel" || true
	docker exec centos7_build bash -c "wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64//mysql-community-devel-5.7.28-1.el7.x86_64.rpm"
	docker exec centos7_build bash -c "wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64//mysql-community-libs-5.7.28-1.el7.x86_64.rpm"
	docker exec centos7_build bash -c "wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64//mysql-community-common-5.7.28-1.el7.x86_64.rpm"
	docker exec centos7_build bash -c "rpm -ihv mysql-community-common-5.7.28-1.el7.x86_64.rpm mysql-community-devel-5.7.28-1.el7.x86_64.rpm mysql-community-libs-5.7.28-1.el7.x86_64.rpm"
	docker exec centos7_build bash -c "wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h"
	docker exec centos7_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp centos7_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-centos7
	docker exec centos7_build bash -c "yum -y install ruby rubygems ruby-devel && gem install fpm && fpm -s dir -t rpm -v1.0 --license GPLv3 --category 'Development/Tools' --rpm-summary 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks.' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	docker cp centos7_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog-1.0-1.x86_64.rpm ./binaries/proxysql-mysqlbinlog-1.0-1-centos7.x86_64.rpm

binaries/proxysql_binlog_reader-ubuntu18:
	docker stop ubuntu18_build || true
	docker rm ubuntu18_build || true
	docker create --name ubuntu18_build renecannao/proxysql:build-ubuntu18 bash -c "while : ; do sleep 10 ; done"
	docker start ubuntu18_build
	docker exec ubuntu18_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp ubuntu18_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-ubuntu18
	docker exec ubuntu18_build bash -c "apt-get update && apt-get -y install ruby rubygems ruby-dev && gem install fpm && fpm -s dir -t deb -v1.0 --license GPLv3 --category 'Development/Tools' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	docker cp ubuntu18_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog_1.0_amd64.deb ./binaries/proxysql-mysqlbinlog_1.0-ubuntu18_amd64.deb


binaries/proxysql_binlog_reader-ubuntu16:
	docker stop ubuntu16_build || true
	docker rm ubuntu16_build || true
	docker create --name ubuntu16_build renecannao/proxysql:build-ubuntu16 bash -c "while : ; do sleep 10 ; done"
	docker start ubuntu16_build
	docker exec ubuntu16_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp ubuntu16_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-ubuntu16
	docker exec ubuntu16_build bash -c "apt-get update && apt-get -y install ruby rubygems ruby-dev && gem install fpm && fpm -s dir -t deb -v1.0 --license GPLv3 --category 'Development/Tools' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	docker cp ubuntu16_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog_1.0_amd64.deb ./binaries/proxysql-mysqlbinlog_1.0-ubuntu16_amd64.deb

binaries/proxysql_binlog_reader-ubuntu14:
	docker stop ubuntu14_build || true
	docker rm ubuntu14_build || true
	docker create --name ubuntu14_build renecannao/proxysql:build-ubuntu14 bash -c "while : ; do sleep 10 ; done"
	docker start ubuntu14_build
	docker exec ubuntu14_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp ubuntu14_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-ubuntu14
	##############################################################################
	## Package build for Ubuntu14 is broken - Ruby version issues in Ubuntu 14: ##
	##############################################################################
	## docker exec ubuntu14_build bash -c "apt-get update && apt-get -y install ruby2.0 ruby2.0-dev && gem2.0 install fpm && fpm -s dir -t deb -v1.0 --license GPLv3 --category 'Development/Tools' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	## docker cp ubuntu14_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog_1.0_amd64.deb ./binaries/proxysql-mysqlbinlog_1.0-ubuntu14_amd64.deb

binaries/proxysql_binlog_reader-debian7:
	docker stop debian7_build || true
	docker rm debian7_build || true
	docker create --name debian7_build renecannao/proxysql:build-debian7 bash -c "while : ; do sleep 10 ; done"
	docker start debian7_build
	docker exec debian7_build bash -c "apt-get update && apt-get -y --force-yes install libboost-all-dev && cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a ; cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp debian7_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-debian7
	## docker exec debian7_build bash -c "apt-get update && apt-get -y install ruby rubygems ruby-dev && gem install fpm && fpm -s dir -t deb -v1.0 --license GPLv3 --category 'Development/Tools' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	## docker cp debian7_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog_1.0_amd64.deb ./binaries/proxysql-mysqlbinlog_1.0-debian7_amd64.deb

binaries/proxysql_binlog_reader-debian8:
	docker stop debian8_build || true
	docker rm debian8_build || true
	docker create --name debian8_build renecannao/proxysql:build-debian8 bash -c "while : ; do sleep 10 ; done"
	docker start debian8_build
	docker exec debian8_build bash -c "apt-get update && apt-get -y --force-yes install libboost-all-dev && cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a ; cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp debian8_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-debian8
	docker exec debian8_build bash -c "apt-get update && apt-get -y install ruby rubygems ruby-dev && gem install fpm && fpm -s dir -t deb -v1.0 --license GPLv3 --category 'Development/Tools' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	docker cp debian8_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog_1.0_amd64.deb ./binaries/proxysql-mysqlbinlog_1.0-debian8_amd64.deb

binaries/proxysql_binlog_reader-debian9:
	docker stop debian9_build || true
	docker rm debian9_build || true
	docker create --name debian9_build renecannao/proxysql:build-debian9 bash -c "while : ; do sleep 10 ; done"
	docker start debian9_build
	docker exec debian9_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a ; cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp debian9_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-debian9
	docker exec debian9_build bash -c "apt-get update && apt-get -y install ruby rubygems ruby-dev && gem install fpm && fpm -s dir -t deb -v1.0 --license GPLv3 --category 'Development/Tools' --description 'ProxySQL is a high performance, high availability, protocol aware proxy for MySQL and forks (like Percona Server and MariaDB). All the while getting the unlimited freedom that comes with a GPL license. Its development is driven by the lack of open source proxies that provide high performance.' --url 'https://proxysql.com' --vendor 'ProxySQL LLC' --debug-workspace --workdir /tmp/ --package=/opt/proxysql_mysqlbinlog/ -n proxysql-mysqlbinlog /opt/proxysql_mysqlbinlog/proxysql_binlog_reader/=/bin/"
	docker cp debian9_build:/opt/proxysql_mysqlbinlog/proxysql-mysqlbinlog_1.0_amd64.deb ./binaries/proxysql-mysqlbinlog_1.0-debian9_amd64.deb

