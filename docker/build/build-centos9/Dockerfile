FROM proxysql/packaging:build-centos9-v3.0.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# set git safe dir
RUN git config --system --add safe.directory /

# fpm packaging
RUN dnf update -y
RUN dnf install -y @ruby:3.1 ruby-devel rubygems && \
	gem install fpm

# libslave needs boost
RUN yum install -y nss libtool boost boost-devel

# mysql 5.7 libs
RUN wget -q http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-devel-5.7.42-1.el7.x86_64.rpm && \
	wget -q http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-libs-5.7.42-1.el7.x86_64.rpm && \
	wget -q http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-common-5.7.42-1.el7.x86_64.rpm && \
	rpm -ihv mysql-community-*.rpm && \
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h

# cleanup rpm cache
RUN yum clean all && \
	rm -rf /var/cache/yum && \
	rm -f *.rpm
