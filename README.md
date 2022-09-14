# ProxySQL - MySQL Binlog Reader

MySQL Binlog reader and parser to be used with ProxySQL and MySQL.

MySQL Binlog reader is a service that runs on MySQL host, tracks GTIDs and provides them to ProxySQL for use in adaptive query routing.
https://www.proxysql.com/blog/proxysql-gtid-causal-reads

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

#### Arguments:

+ `-h`: MySQL host
+ `-u`: MySQL username
+ `-p`: MySQL password
+ `-P`: MySQL port
+ `-l`: listening port
+ `-f`: run in foreground - all logging goes to stdout/stderr
+ `-L`: path to log file

configure ProxySQL `mysql_servers` with coresponding `gtid_port` for each server, and also `mysql_replication_hostgroups`:

```
INSERT INTO mysql_servers (hostgroup_id,hostname,gtid_port,port,max_replication_lag,comment) VALUES (10,'mysql1',6020,3306,1,'mysql1');
INSERT INTO mysql_servers (hostgroup_id,hostname,gtid_port,port,max_replication_lag,comment) VALUES (20,'mysql2',6020,3306,1,'mysql2');
INSERT INTO mysql_servers (hostgroup_id,hostname,gtid_port,port,max_replication_lag,comment) VALUES (20,'mysql3',6020,3306,1,'mysql3');
INSERT INTO mysql_replication_hostgroups (writer_hostgroup,reader_hostgroup,comment) VALUES (10,20,'gtid_replication');
LOAD MYSQL SERVERS TO RUNTIME;
```

monitor the ProxySQL `stats_mysql_gtid_executed` table for executed GTIDs:

```
SELECT hostname,gtid_executed FROM stats_mysql_gtid_executed\G

*************************** 1. row ***************************
     hostname: mysql1
gtid_executed: 85c17137-4258-11e8-8090-0242ac130002:1-146301
*************************** 2. row ***************************
     hostname: mysql2
gtid_executed: 85c17137-4258-11e8-8090-0242ac130002:1-146300,8a093f5f-4258-11e8-8037-0242ac130004:1-5
*************************** 3. row ***************************
     hostname: mysql3
gtid_executed: 85c17137-4258-11e8-8090-0242ac130002:1-146301,8a0ac961-4258-11e8-8003-0242ac130003:1-5
```


### More

https://proxysql.com/blog/proxysql-gtid-causal-reads/
