FROM ubuntu:22.04

LABEL authors="Miro Stauder <miro@proxysql.com>"

# dependencies
RUN apt update -y && \
	apt install -y \
	wget

# install PPA:nrbrtx/libssl1
RUN wget -O /etc/apt/keyrings/nrbrtx.asc 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x19bfcaf5168d33a9' && \
	echo "deb [signed-by=/etc/apt/keyrings/nrbrtx.asc] https://ppa.launchpadcontent.net/nrbrtx/libssl1/ubuntu jammy main" > /etc/apt/sources.list.d/nrbrt.list

# install libssl1.1
RUN apt update && \
	apt install -y \
	libssl1.1

# copy package from context
COPY proxysql-mysqlbinlog_*.deb ./
RUN bash -c "yes | dpkg -i proxysql-mysqlbinlog_*$(dpkg --print-architecture).deb" && \
	rm -f proxysql-mysqlbinlog_*.deb

CMD ["sh", "-c", "proxysql_binlog_reader -h \"${MYSQL_HOST:-127.0.0.1}\" -u \"${MYSQL_USER:=root}\" -p \"${MYSQL_PASSWORD:-root}\" -P \"${MYSQL_PORT:-3306}\" -l \"${LISTEN_PORT:-6020}\" -f"]
