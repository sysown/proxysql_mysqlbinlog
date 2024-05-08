FROM proxysql/packaging:build-debian10

MAINTAINER Miro Stauder <miro@proxysql.com>

# set git safe dir
RUN git config --system --add safe.directory /opt/

# mysql 5.7 libs
RUN apt-get -y update && \
	apt-get -y purge mysql-client libmariadbclient-dev libmariadb-dev && \
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient20_5.7.37-1debian10_amd64.deb && \
	dpkg -i libmysqlclient20_5.7.37-1debian10_amd64.deb && \
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient-dev_5.7.37-1debian10_amd64.deb && \
	dpkg -i libmysqlclient-dev_5.7.37-1debian10_amd64.deb && \
	apt-get -y install libboost-all-dev && \
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h

# fpm packaging
RUN apt-get -y install ruby ruby-dev && \
	gem install --no-ri --no-rdoc dotenv -v '2.8.1' && \
	gem install --no-ri --no-rdoc ffi -v '1.9.14' && \
	gem install --no-ri --no-rdoc fpm -v '1.11.0'
