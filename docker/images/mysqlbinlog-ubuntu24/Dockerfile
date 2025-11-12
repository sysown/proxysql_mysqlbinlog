FROM ubuntu:24.04

LABEL authors="Miro Stauder <miro@proxysql.com>"

# copy package from context
COPY proxysql-mysqlbinlog_*.deb ./
RUN bash -c "yes | dpkg -i proxysql-mysqlbinlog_*$(dpkg --print-architecture).deb"

# clean apt cache
RUN apt clean && \
	rm -rf /var/cache/apt/* && \
	rm -rf /var/lib/apt/lists/* && \
	rm -f *.deb

CMD ["sh", "-c", "proxysql_binlog_reader -h \"${MYSQL_HOST:-127.0.0.1}\" -u \"${MYSQL_USER:=root}\" -p \"${MYSQL_PASSWORD:-root}\" -P \"${MYSQL_PORT:-3306}\" -l \"${LISTEN_PORT:-6020}\" -f"]
