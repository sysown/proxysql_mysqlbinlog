# ProxySQL - MySQL Binlog Reader

MySQL Binlog reader and parser to be used with ProxySQL

### Requirements

building requires docker.io, install as per instructions:

https://docs.docker.com/engine/install/

### Building

Currently supported targets are:

- build
  - build-centos
    - build-centos7
    - build-centos8
  - build-debian
    - build-debian9
    - build-debian10
    - build-debian11
  - build-ubuntu
    - build-ubuntu16
    - build-ubuntu18
    - build-ubuntu20

to build do:
```
cd proxysql_mysqlbinlog
make build-ubuntu18
```

executable and package can be found in `./binaries`
```
-rw-r--r-- 1 root root 1679864 May  2 15:56 proxysql-mysqlbinlog_2.0-0-g663ab16-ubuntu18_amd64.deb
-rwxr-xr-x 1 root root 5717720 May  2 15:54 proxysql_binlog_reader-2.0-0-g663ab16-ubuntu18
```

### Docker Images

https://hub.docker.com/r/proxysql/proxysql-mysqlbinlog

### HowTo

run the command with parameters, e.g:

./proxysql_binlog_reader -h ${MYSQL_HOST:-127.0.0.1} -u ${MYSQL_USER:=root} -p ${MYSQL_PASSWORD:-root} -P ${MYSQL_PORT:-3306} -l ${LISTEN_PORT:-6020} -f



