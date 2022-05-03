FROM ubuntu:16.04

MAINTAINER Miro Stauder <miro@proxysql.com>

# dependencies
RUN apt-get -y update && \
	apt-get -y install \
	libssl1.0.0

# copy package from context
COPY proxysql-mysqlbinlog_*.deb ./
RUN yes | dpkg -i proxysql-mysqlbinlog_*.deb && \
	rm -f proxysql-mysqlbinlog_*.deb

CMD ["sh", "-c", "proxysql_binlog_reader -h \"${MYSQL_HOST:-127.0.0.1}\" -u \"${MYSQL_USER:=root}\" -p \"${MYSQL_PASSWORD:-root}\" -P \"${MYSQL_PORT:-3306}\" -l \"${LISTEN_PORT:-6020}\" -f"]
