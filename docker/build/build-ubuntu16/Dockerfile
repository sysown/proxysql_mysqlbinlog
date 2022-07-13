FROM proxysql/packaging:build-ubuntu16

MAINTAINER Miro Stauder <miro@proxysql.com>

# set git safe dir
RUN git config --system --add safe.directory /opt/

# fpm packaging
RUN apt-get -y update && \
	apt-get -y install ruby ruby-dev && \
	gem install --no-ri --no-rdoc ffi -v '1.9.14' && \
	gem install --no-ri --no-rdoc fpm -v '1.11.0'
