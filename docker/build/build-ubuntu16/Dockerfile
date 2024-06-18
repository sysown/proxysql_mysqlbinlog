FROM proxysql/packaging:build-ubuntu16

MAINTAINER Miro Stauder <miro@proxysql.com>

# set git safe dir
RUN git config --system --add safe.directory /opt/

# build dependencies
RUN apt-get -y update && \
	apt-get -y install libboost-all-dev

# fpm packaging
RUN apt-get -y install ruby ruby-dev && \
	gem install --no-ri --no-rdoc dotenv -v '2.8.1' && \
	gem install --no-ri --no-rdoc ffi -v '1.9.14' && \
	gem install --no-ri --no-rdoc fpm -v '1.12.0'
