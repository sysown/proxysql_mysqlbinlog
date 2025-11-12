FROM proxysql/packaging:build-ubuntu16-v2.5.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# set git safe dir
RUN git config --system --add safe.directory /opt/

# fpm packaging
RUN apt-get -y update && \
	apt-get -y install ruby ruby-dev ruby-ffi ruby-dotenv && \
	gem install --no-ri --no-rdoc fpm -v '1.11.0'

# clean apt cache
RUN apt clean && \
	rm -rf /var/cache/apt/* && \
	rm -rf /var/lib/apt/lists/*
