FROM proxysql/packaging:build-centos8

MAINTAINER Miro Stauder <miro@proxysql.com>

# set git safe dir
RUN git config --system --add safe.directory /

# mysql 5.7 libs
RUN yum update -y && \
	yum install -y nss libtool boost boost-devel && \
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-devel-5.7.28-1.el7.x86_64.rpm && \
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-libs-5.7.28-1.el7.x86_64.rpm && \
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-common-5.7.28-1.el7.x86_64.rpm && \
	rpm -ihv mysql-community-common-5.7.28-1.el7.x86_64.rpm mysql-community-devel-5.7.28-1.el7.x86_64.rpm mysql-community-libs-5.7.28-1.el7.x86_64.rpm && \
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h

# fpm packaging
RUN yum install -y ruby rubygems ruby-devel && \
	gem install ffi && \
	gem install fpm
