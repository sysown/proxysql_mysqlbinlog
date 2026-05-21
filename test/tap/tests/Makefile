# Build *-t.cpp tests against ../libtap.a.

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -g -O0 -Wall -Wextra
MYSQL_CFLAGS ?= $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS   ?= $(shell mysql_config --libs 2>/dev/null)

TEST_SRCS = $(wildcard *-t.cpp)
TEST_BINS = $(TEST_SRCS:.cpp=)

.PHONY: default clean
default: $(TEST_BINS)

%-t: %-t.cpp ../libtap.a
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) -I.. -I../../.. $< ../libtap.a $(MYSQL_LIBS) -lpthread -o $@

clean:
	rm -f $(TEST_BINS)
