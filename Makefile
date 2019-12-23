.PHONY: default
default: proxysql_binlog_reader


ubuntu18: binaries/proxysql_binlog_reader-ubuntu18
.PHONY: ubuntu18

ubuntu16: binaries/proxysql_binlog_reader-ubuntu16
.PHONY: ubuntu16

ubuntu14: binaries/proxysql_binlog_reader-ubuntu14
.PHONY: ubuntu14

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

binaries/proxysql_binlog_reader-ubuntu18:
	docker stop ubuntu18_build || true
	docker rm ubuntu18_build || true
	docker create --name ubuntu18_build renecannao/proxysql:build-ubuntu18 bash -c "while : ; do sleep 10 ; done"
	docker start ubuntu18_build
	docker exec ubuntu18_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp ubuntu18_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-ubuntu18

binaries/proxysql_binlog_reader-ubuntu16:
	docker stop ubuntu16_build || true
	docker rm ubuntu16_build || true
	docker create --name ubuntu16_build renecannao/proxysql:build-ubuntu16 bash -c "while : ; do sleep 10 ; done"
	docker start ubuntu16_build
	docker exec ubuntu16_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp ubuntu16_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-ubuntu16

binaries/proxysql_binlog_reader-ubuntu14:
	docker stop ubuntu14_build || true
	docker rm ubuntu14_build || true
	docker create --name ubuntu14_build renecannao/proxysql:build-ubuntu14 bash -c "while : ; do sleep 10 ; done"
	docker start ubuntu14_build
	docker exec ubuntu14_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a && cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp ubuntu14_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-ubuntu14

binaries/proxysql_binlog_reader-debian7:
	docker stop debian7_build || true
	docker rm debian7_build || true
	docker create --name debian7_build renecannao/proxysql:build-debian7 bash -c "while : ; do sleep 10 ; done"
	docker start debian7_build
	docker exec debian7_build bash -c "cd /opt; git clone https://github.com/sysown/proxysql_mysqlbinlog.git && cd /opt/proxysql_mysqlbinlog/libslave/ && cmake . && make slave_a ; cd /opt/proxysql_mysqlbinlog && make"
	sleep 2
	docker cp debian7_build:/opt/proxysql_mysqlbinlog/proxysql_binlog_reader ./binaries/proxysql_binlog_reader-debian7
