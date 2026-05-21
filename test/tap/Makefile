# Build libtap.a + the per-test binaries under tests/.
#
# This Makefile produces a static archive of the TAP helpers and then
# delegates to tests/Makefile to build the *-t binaries.

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -g -O0 -Wall -Wextra
MYSQL_CFLAGS ?= $(shell mysql_config --cflags 2>/dev/null)

LIB_SRCS = tap.cpp command_line.cpp binlog_reader_process.cpp \
           binlog_reader_client.cpp mysql_client.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)

# proxysql_gtid.{h,cpp} lives at the repo root and is shared with the
# reader. We compile it here so libtap.a is self-contained.
GTID_OBJ = proxysql_gtid.o

.PHONY: default lib tests clean
default: lib tests

lib: libtap.a

libtap.a: $(LIB_OBJS) $(GTID_OBJ)
	ar rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) -I. -I../.. -c $< -o $@

proxysql_gtid.o: ../../proxysql_gtid.cpp ../../proxysql_gtid.h
	$(CXX) $(CXXFLAGS) -I../.. -c $< -o $@

tests: libtap.a
	$(MAKE) -C tests

clean:
	rm -f $(LIB_OBJS) $(GTID_OBJ) libtap.a
	$(MAKE) -C tests clean
