# Build *-t.cpp tests against ../libtap.a.

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -g -O0 -Wall -Wextra

.DEFAULT_GOAL := default

include ../mariadb.mk

TEST_SRCS = $(wildcard *-t.cpp)
TEST_BINS = $(TEST_SRCS:.cpp=)

.PHONY: default clean
default: $(TEST_BINS)

test_replication_server_id-t: test_replication_server_id-t.cpp \
		../../../mariadb_replication_client.cpp ../../../mariadb_replication.cpp \
		../../../tls_options.h ../../../tls_options.cpp \
		../../../proxysql_gtid.cpp ../tap.cpp $(MARIADB_CONNECTOR_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. \
		test_replication_server_id-t.cpp ../../../mariadb_replication_client.cpp \
		../../../mariadb_replication.cpp ../../../tls_options.cpp \
		../../../proxysql_gtid.cpp ../tap.cpp \
		$(MARIADB_LIBS) -lpthread -o $@

test_replication_timeouts-t: test_replication_timeouts-t.cpp \
		../../../mariadb_replication_client.cpp ../../../mariadb_replication.cpp \
		../../..//tls_options.cpp ../../../proxysql_gtid.cpp \
		../tap.cpp $(MARIADB_CONNECTOR_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. \
		test_replication_timeouts-t.cpp ../../../mariadb_replication_client.cpp \
		../../../mariadb_replication.cpp ../../../tls_options.cpp \
		../../../proxysql_gtid.cpp ../tap.cpp \
		$(MARIADB_LIBS) -lpthread -o $@

test_tls_options-t: test_tls_options-t.cpp ../../../tls_options.h \
		../../../tls_options.cpp ../tap.h ../tap.cpp $(MARIADB_CONNECTOR_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. \
		test_tls_options-t.cpp ../../../tls_options.cpp ../tap.cpp \
		$(MARIADB_LIBS) -lpthread -o $@

test_tls_connector_options-t: test_tls_connector_options-t.cpp ../../../tls_options.h \
		../../../tls_options.cpp ../tap.h ../tap.cpp $(MARIADB_CONNECTOR_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. \
		test_tls_connector_options-t.cpp ../../../tls_options.cpp ../tap.cpp \
		$(MARIADB_LIBS) -lpthread -o $@

%-t: %-t.cpp ../libtap.a $(MARIADB_CONNECTOR_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. $< ../libtap.a $(MARIADB_LIBS) -lpthread -o $@

clean:
	rm -f $(TEST_BINS)
