FROM proxysql/packaging:build-debian9-v2.5.3

MAINTAINER Miro Stauder <miro@proxysql.com>

# fix repos
RUN sed -i '/stretch-updates/d' /etc/apt/sources.list; \
  sed -i 's/deb.debian/archive.debian/' /etc/apt/sources.list; \
  sed -i 's/security.debian/archive.debian/' /etc/apt/sources.list; \
  sed -i 's/deb /deb [trusted=yes] /' /etc/apt/sources.list

# set git safe dir
RUN git config --system --add safe.directory /opt/

# mysql 5.7 libs
RUN apt-get -y update && \
	apt-get -y purge mysql-client libmariadbclient-dev libmariadb-dev && \
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient20_5.7.34-1debian9_amd64.deb && \
	dpkg -i libmysqlclient20_5.7.34-1debian9_amd64.deb && \
	wget http://repo.mysql.com/apt/debian/pool/mysql-5.7/m/mysql-community/libmysqlclient-dev_5.7.34-1debian9_amd64.deb && \
	dpkg -i libmysqlclient-dev_5.7.34-1debian9_amd64.deb && \
	apt-get -y install libboost-all-dev && \
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h

# fpm packaging
RUN apt-get -y install ruby ruby-dev ruby-ffi ruby-dotenv && \
	gem install fpm -v '1.11.0'
