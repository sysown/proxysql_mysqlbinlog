FROM quay.io/centos/centos:9

LABEL authors="Miro Stauder <miro@proxysql.com>"

# fix keys
#RUN rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-centosofficial

# fix repos
#RUN sed -i 's/mirrorlist/#mirrorlist/' /etc/yum.repos.d/CentOS-*.repo
#RUN sed -i 's/#baseurl/baseurl/' /etc/yum.repos.d/CentOS-*.repo
#RUN sed -i 's/mirror./vault./' /etc/yum.repos.d/CentOS-*.repo

# Runtime OpenSSL libraries required by the reader.
RUN yum -y update && \
	yum -y install openssl-libs && \
	yum clean all && \
	rm -rf /var/cache/yum

# copy package from context
COPY proxysql-mysqlbinlog-*.rpm ./
RUN bash -c "rpm -i proxysql-mysqlbinlog-*$(rpm --eval '%{_arch}').rpm"

# Remove the package copied into the image build context.
RUN rm -f *.rpm

CMD ["sh", "-ec", "set -- proxysql_binlog_reader -h \"${MYSQL_HOST:-127.0.0.1}\" -u \"${MYSQL_USER:=root}\" -p \"${MYSQL_PASSWORD:-root}\" -P \"${MYSQL_PORT:-3306}\" -l \"${LISTEN_PORT:-6020}\" -t \"${UPDATE_FREQ_MS:-0}\" -b \"${BATCHING:-1}\" -f --ssl-mode \"${SSL_MODE:-REQUIRED}\" --ssl-verify-server-cert \"${SSL_VERIFY_SERVER_CERT:-1}\"; [ -z \"${SSL_CA:-}\" ] || set -- \"$@\" --ssl-ca \"$SSL_CA\"; [ -z \"${SSL_CAPATH:-}\" ] || set -- \"$@\" --ssl-capath \"$SSL_CAPATH\"; [ -z \"${SSL_CERT:-}\" ] || set -- \"$@\" --ssl-cert \"$SSL_CERT\"; [ -z \"${SSL_KEY:-}\" ] || set -- \"$@\" --ssl-key \"$SSL_KEY\"; [ -z \"${SSL_CIPHER:-}\" ] || set -- \"$@\" --ssl-cipher \"$SSL_CIPHER\"; [ -z \"${TLS_VERSION:-}\" ] || set -- \"$@\" --tls-version \"$TLS_VERSION\"; exec \"$@\""]
