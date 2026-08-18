# Build libtap.a + the per-test binaries under tests/.
#
# This Makefile produces a static archive of the TAP helpers and then
# delegates to tests/Makefile to build the *-t binaries.

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -g -O0 -Wall -Wextra

.DEFAULT_GOAL := default

include mariadb.mk

LIB_SRCS = tap.cpp command_line.cpp binlog_reader_process.cpp \
           binlog_reader_client.cpp mysql_client.cpp \
           ../../mariadb_replication.cpp ../../tls_options.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)

# proxysql_gtid.{h,cpp} lives at the repo root and is shared with the
# reader. We compile it here so libtap.a is self-contained.
GTID_OBJ = proxysql_gtid.o

.PHONY: default lib tests clean
default: lib tests

lib: libtap.a

libtap.a: $(MARIADB_CONNECTOR_ARCHIVE) $(LIB_OBJS) $(GTID_OBJ) \
          ../../tls_options.cpp ../../tls_options.h
	ar rcs $@ $(LIB_OBJS) $(GTID_OBJ)

$(LIB_OBJS): $(MARIADB_CONNECTOR_ARCHIVE)

../../tls_options.o: ../../tls_options.cpp ../../tls_options.h

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I. -I../.. -c $< -o $@

proxysql_gtid.o: ../../proxysql_gtid.cpp ../../proxysql_gtid.h
	$(CXX) $(CXXFLAGS) -I../.. -c $< -o $@

tests: libtap.a
	$(MAKE) -C tests

clean:
	rm -f $(LIB_OBJS) $(GTID_OBJ) libtap.a
	$(MAKE) -C tests clean
