# Build *-t.cpp tests against ../libtap.a.

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -g -O0 -Wall -Wextra
MARIADB_CFLAGS ?= $(shell mariadb_config --cflags 2>/dev/null)
MARIADB_LIBS   ?= $(shell mariadb_config --libs 2>/dev/null)

TEST_SRCS = $(wildcard *-t.cpp)
TEST_BINS = $(TEST_SRCS:.cpp=)

.PHONY: default clean
default: $(TEST_BINS)

%-t: %-t.cpp ../libtap.a
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. $< ../libtap.a $(MARIADB_LIBS) -lpthread -o $@

clean:
	rm -f $(TEST_BINS)
