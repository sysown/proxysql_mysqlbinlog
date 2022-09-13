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

### Containers

Ready to use docker containers:

https://hub.docker.com/r/proxysql/proxysql-mysqlbinlog

### HowTo

on each MySQL server instance run the `proxysql_binlog_reader`, e.g:

```
./proxysql_binlog_reader -h 127.0.0.1 -u root -p rootpass -P 3306 -l 6020 -f
```

configure ProxySQL `mysql_servers` with coresponding `gtid_port` for each server:

```
INSERT INTO mysql_servers (hostgroup_id,hostname,gtid_port,port,max_replication_lag,comment) VALUES (10,'127.0.0.1',6020,3306,1,'mysql1');
```
### More

https://proxysql.com/blog/proxysql-gtid-causal-reads/
