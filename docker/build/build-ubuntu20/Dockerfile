FROM proxysql/packaging:build-ubuntu20

MAINTAINER Miro Stauder <miro@proxysql.com>

# set git safe dir
RUN git config --system --add safe.directory /opt/

# mysql 5.7 libs
RUN apt-get -y update && \
	apt-get -y purge mysql-client libmariadbclient-dev libmariadb-dev && \
	wget -q https://repo.mysql.com/apt/ubuntu/pool/mysql-5.7/m/mysql-community/libmysqlclient20_5.7.38-1ubuntu18.04_amd64.deb && \
	dpkg -i libmysqlclient20_5.7.38-1ubuntu18.04_amd64.deb && \
	wget -q https://repo.mysql.com/apt/ubuntu/pool/mysql-5.7/m/mysql-community/libmysqlclient-dev_5.7.38-1ubuntu18.04_amd64.deb && \
	dpkg -i libmysqlclient-dev_5.7.38-1ubuntu18.04_amd64.deb && \
	apt-get -y install libboost-all-dev && \
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h

# fpm packaging
RUN apt-get -y install ruby ruby-dev && \
	gem install ffi && \
	gem install fpm
