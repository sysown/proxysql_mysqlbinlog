FROM proxysql/packaging:build-debian12-v3.0.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# set git safe dir
RUN git config --system --add safe.directory /opt/

# fpm packaging
RUN apt-get -y update && \
	apt-get -y install ruby ruby-dev ruby-ffi ruby-dotenv && \
	gem install fpm

# Build the vendored Connector/C library.
RUN apt-get -y install cmake libssl-dev

# clean apt cache
RUN apt clean && \
	rm -rf /var/cache/apt/* && \
	rm -rf /var/lib/apt/lists/* && \
	rm -f *.deb
