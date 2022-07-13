FROM proxysql/packaging:build-ubuntu18

MAINTAINER Miro Stauder <miro@proxysql.com>

# set git safe dir
RUN git config --system --add safe.directory /opt/

# fpm packaging
RUN apt-get -y update && \
	apt-get -y install ruby ruby-dev && \
	gem install ffi && \
	gem install fpm
