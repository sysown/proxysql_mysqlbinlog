# ProxySQL MySQl Binlog reader

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
  - build-ubuntu
    - build-ubuntu16
    - build-ubuntu18

to build do:
```
cd proxysql_mysqlbinlog
make build-ubuntu18`
```

executable and package can be found in `./binaries`
```
-rw-r--r-- 1 root root 1679864 May  2 15:56 proxysql-mysqlbinlog_2.0-0-g663ab16-ubuntu18_amd64.deb
-rwxr-xr-x 1 root root 5717720 May  2 15:54 proxysql_binlog_reader-2.0-0-g663ab16-ubuntu18
```


