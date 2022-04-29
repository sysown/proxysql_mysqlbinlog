FROM centos:7

MAINTAINER Miro Stauder <miro@proxysql.com>

# copy package from context
COPY proxysql-mysqlbinlog-*.rpm ./
RUN rpm -i proxysql-mysqlbinlog-*.rpm && \
	rm -f proxysql-mysqlbinlog-*.rpm

CMD ["/bin/sh" "-c" "/bin/proxysql_binlog_reader   -h \"${MYSQL_HOST:-127.0.0.1}\"   -u \"${MYSQL_USER:=root}\"   -p \"${MYSQL_PASSWORD:-root}\"   -P \"${MYSQL_PORT:-3306}\"   -i \"${LISTEN_ADDR:-0.0.0.0}\"   -l \"${LISTEN_PORT:-6020}\"   -f"]
