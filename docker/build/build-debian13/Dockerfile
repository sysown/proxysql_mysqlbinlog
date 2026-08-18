FROM proxysql/packaging:build-debian13-v3.0.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# set git safe dir
RUN git config --system --add safe.directory /opt/

# fpm packaging
RUN apt-get -y update && \
	apt-get -y install ruby ruby-dev ruby-ffi ruby-dotenv && \
	gem install fpm

# Connector/C provides authentication plugins and the replication API.
RUN apt-get -y install libmariadb-dev

# clean apt cache
RUN apt clean && \
	rm -rf /var/cache/apt/* && \
	rm -rf /var/lib/apt/lists/* && \
	rm -f *.deb
