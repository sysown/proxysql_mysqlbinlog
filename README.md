# proxysql_mysqlbinlog

`proxysql_mysqlbinlog` is a service that runs on MySQL host, tracks GTID and reports it to ProxySQL.
It was built to complement ProxySQL's [Adaptive query routing based on GTID tracking](https://www.proxysql.com/blog/proxysql-gtid-causal-reads) feature.

## Install

The project is built using Makefile. There's also a Docker image available with pre-built sources (`renecannao/proxysql_mysqlbinlog:ubuntu18`).

## Usage

```
$ proxysql_binlog_reader -h <mysql-host> -u <mysql-user> -l <port-to-listen> -f
# or
$ docker run renecannao/proxysql_mysqlbinlog:ubuntu18 -p 3310:3310 /proxysql_binlog_reader -h <mysql-host> -u <mysql-user> -l <port-to-listen> -f
```

Arguments:

* `-h`: MySQL host
* `-u`: MySQL username
* `-p`: MySQL password
* `-P`: MySQL port
* `-l`: Port to listen and to serve GTID stats
* `-f`: (optional, recommended for container setup) run in foreground as opposite to daemonizing
* `-L`: (optional) path to log file

## Setting up with ProxySQL

0. Make sure you're running ProxySQL >= 2.0
1. Setup `proxysql_mysqlbinlog` to run on MySQL host, confirm that it's listening on the specified port and serves stats (easy to check with `nc <host> <port>`)
2. In ProxySQL configuration, add `gtid_port` to the host configuration, pointing to the port that `proxysql_mysqlbinlog` listens on. Note that ProxySQL expects `proxysql_mysqlbinlog` to run on the same hostname as MySQL, but on the different port.
3. Enter ProxySQL admin and inspect `stats_mysql_gtid_executed` table. You'll be supposed to see GTIDs being updated in ProxySQL's memory.

```
Admin> select hostname,gtid_executed from stats_mysql_gtid_executed order by hostname\G
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
