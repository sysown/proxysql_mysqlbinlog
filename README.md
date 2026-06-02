# ProxySQL - MySQL Binlog Reader

MySQL Binlog reader and parser to be used with ProxySQL and MySQL.

MySQL Binlog reader is a service that runs on MySQL host, tracks GTIDs and provides them to ProxySQL for use in adaptive query routing.
https://www.proxysql.com/blog/proxysql-gtid-causal-reads

### Requirements

- bulding natively requires a proper build enviroment, consult your distro documentation.
- building packages requires `docker compose`, builds inside a docker container
- building docker images requires `docker buildx`

install docker as per instructions:

https://docs.docker.com/engine/install/

### Building

Currently supported distros are:

- CentOS 10, 9, 8, 7
- Debian 13, 12, 11, 10, 9
- Ubuntu 24, 22, 20, 18, 16

e.g.: to build an executable and a package for Ubuntu 24 do:
```
cd proxysql_mysqlbinlog
make ubuntu24
```

executable and package can be found in `./binaries`
```
-rw-r--r-- 1 root root 1679864 Nov  24 15:56 proxysql-mysqlbinlog_2.2-13-g12758a2-ubuntu24_amd64.deb
-rwxr-xr-x 1 root root 5717720 Nov  24 15:54 proxysql_binlog_reader-2.2-13-g12758a2-ubuntu24
```

### Containers

Ready to use v2.3 docker images, published to the GitHub Container Registry (GHCR):

- 2.3-centos10 == centos10 == centos
- 2.3-centos9 == centos9
- 2.3-debian13 == debian13 == debian == latest == 2.3
- 2.3-debian12 == debian12
- 2.3-ubuntu24 == ubuntu24 == ubuntu
- 2.3-ubuntu22 == ubuntu22

Semantic tag versioning is used, ommited version implies latest.

https://github.com/sysown/proxysql_mysqlbinlog/pkgs/container/proxysql-mysqlbinlog

use `docker pull ghcr.io/sysown/proxysql-mysqlbinlog:latest` to pull the latest docker image

#### Container configuration

The image entrypoint runs `proxysql_binlog_reader` in the foreground, configured via environment variables:

| Variable | Maps to | Default |
| --- | --- | --- |
| `MYSQL_HOST` | `-h` | `127.0.0.1` |
| `MYSQL_PORT` | `-P` | `3306` |
| `MYSQL_USER` | `-u` | `root` |
| `MYSQL_PASSWORD` | `-p` | `root` |
| `LISTEN_PORT` | `-l` | `6020` |
| `UPDATE_FREQ_MS` | `-t` | `0` |
| `BATCHING` | `-b` | `1` |

e.g.:
```
docker run -d --name binlog-reader -p 6020:6020 \
  -e MYSQL_HOST=mysql1 -e MYSQL_USER=root -e MYSQL_PASSWORD=secret \
  ghcr.io/sysown/proxysql-mysqlbinlog:latest
```

### HowTo

on each MySQL server instance run the `proxysql_binlog_reader`, e.g:

```
./proxysql_binlog_reader -h 127.0.0.1 -u root -p rootpass -P 3306 -l 6020 -f
```

#### Arguments

+ `-h`: MySQL host
+ `-u`: MySQL username
+ `-p`: MySQL password
+ `-P`: MySQL port
+ `-l`: listening port
+ `-f`: run in foreground - all logging goes to stdout/stderr
+ `-L`: path to log file
+ `-t`: optional update throttling, in milliseconds (default 0 - update on every event)
+ `-b`: update batching, 0 or 1 (default 1); set to 0 for ProxySQL servers older than v3.0.8
+ `-B`: optional maximum network buffer size, in bytes
+ `-v`: output build version


#### Configuration

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
