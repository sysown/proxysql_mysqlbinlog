FROM centos:8

MAINTAINER Miro Stauder <miro@proxysql.com>

# fix keys
RUN rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-centosofficial

# fix repos
RUN sed -i 's/mirrorlist/#mirrorlist/' /etc/yum.repos.d/CentOS-*.repo
RUN sed -i 's/#baseurl/baseurl/' /etc/yum.repos.d/CentOS-*.repo
RUN sed -i 's/mirror./vault./' /etc/yum.repos.d/CentOS-*.repo

RUN yum -y update

# dependencies
RUN yum -y install \
	boost-system

# copy package from context
COPY proxysql-mysqlbinlog-*.rpm ./
RUN rpm -i proxysql-mysqlbinlog-*.rpm && \
	rm -f proxysql-mysqlbinlog-*.rpm

CMD ["sh", "-c", "proxysql_binlog_reader -h \"${MYSQL_HOST:-127.0.0.1}\" -u \"${MYSQL_USER:=root}\" -p \"${MYSQL_PASSWORD:-root}\" -P \"${MYSQL_PORT:-3306}\" -l \"${LISTEN_PORT:-6020}\" -f"]
