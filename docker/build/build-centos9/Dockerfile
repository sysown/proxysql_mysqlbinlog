FROM proxysql/packaging:build-centos9-v3.0.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# set git safe dir
RUN git config --system --add safe.directory /

# fpm packaging
RUN dnf update -y
RUN dnf install -y @ruby:3.1 ruby-devel rubygems && \
	gem install fpm

# Connector/C provides authentication plugins and the replication API.
RUN dnf install -y mariadb-connector-c-devel

# cleanup rpm cache
RUN yum clean all && \
	rm -rf /var/cache/yum
