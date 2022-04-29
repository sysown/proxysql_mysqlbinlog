FROM debian:9

MAINTAINER Miro Stauder <miro@proxysql.com>

# copy package from context
COPY proxysql-mysqlbinlog_*.deb ./
RUN yes | dpkg -i proxysql-mysqlbinlog_*.deb && \
	rm -f proxysql-mysqlbinlog_*.deb

CMD ["/bin/sh" "-c" "/bin/proxysql_binlog_reader   -h \"${MYSQL_HOST:-127.0.0.1}\"   -u \"${MYSQL_USER:=root}\"   -p \"${MYSQL_PASSWORD:-root}\"   -P \"${MYSQL_PORT:-3306}\"   -i \"${LISTEN_ADDR:-0.0.0.0}\"   -l \"${LISTEN_PORT:-6020}\"   -f"]
