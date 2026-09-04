FROM debian:12

LABEL authors="Miro Stauder <miro@proxysql.com>"
LABEL org.opencontainers.image.source="https://github.com/sysown/proxysql_mysqlbinlog"

# Runtime OpenSSL libraries required by the reader.
RUN apt-get -y update && \
	apt-get -y install --no-install-recommends libssl3 && \
	rm -rf /var/lib/apt/lists/*

# copy package from context
COPY proxysql-mysqlbinlog_*.deb ./
RUN bash -c "yes | dpkg -i proxysql-mysqlbinlog_*$(dpkg --print-architecture).deb"

# clean apt cache
RUN apt clean && \
	rm -rf /var/cache/apt/* && \
	rm -f *.deb

CMD ["sh", "-ec", "set -- proxysql_binlog_reader -h \"${MYSQL_HOST:-127.0.0.1}\" -u \"${MYSQL_USER:=root}\" -p \"${MYSQL_PASSWORD:-root}\" -P \"${MYSQL_PORT:-3306}\" -l \"${LISTEN_PORT:-6020}\" -t \"${UPDATE_FREQ_MS:-0}\" -b \"${BATCHING:-1}\" -f --ssl-mode \"${SSL_MODE:-REQUIRED}\" --ssl-verify-server-cert \"${SSL_VERIFY_SERVER_CERT:-1}\"; [ -z \"${SSL_CA:-}\" ] || set -- \"$@\" --ssl-ca \"$SSL_CA\"; [ -z \"${SSL_CAPATH:-}\" ] || set -- \"$@\" --ssl-capath \"$SSL_CAPATH\"; [ -z \"${SSL_CERT:-}\" ] || set -- \"$@\" --ssl-cert \"$SSL_CERT\"; [ -z \"${SSL_KEY:-}\" ] || set -- \"$@\" --ssl-key \"$SSL_KEY\"; [ -z \"${SSL_CIPHER:-}\" ] || set -- \"$@\" --ssl-cipher \"$SSL_CIPHER\"; [ -z \"${TLS_VERSION:-}\" ] || set -- \"$@\" --tls-version \"$TLS_VERSION\"; exec \"$@\""]
