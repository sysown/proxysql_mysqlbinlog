FROM proxysql/packaging:build-centos9-v3.0.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# set git safe dir
RUN git config --system --add safe.directory /

# fpm packaging
RUN dnf update -y
RUN dnf install -y @ruby:3.1 ruby-devel rubygems && \
	gem install fpm

# Build the vendored Connector/C library.
RUN dnf install -y cmake openssl-devel

# cleanup rpm cache
RUN yum clean all && \
	rm -rf /var/cache/yum
