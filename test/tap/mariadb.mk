# Vendored MariaDB Connector/C configuration shared by the TAP framework and
# per-test binaries. Resolve paths from this file so inclusion works from any
# make invocation directory.
TAP_MARIADB_MK := $(lastword $(MAKEFILE_LIST))
TAP_DIR := $(abspath $(dir $(TAP_MARIADB_MK)))
ROOT_DIR := $(abspath $(TAP_DIR)/../..)

MARIADB_CONNECTOR_DIR := $(ROOT_DIR)/mariadb-connector-c-3.4.8
MARIADB_CONNECTOR_ARCHIVE := $(MARIADB_CONNECTOR_DIR)/build/libmariadb/libmariadbclient.a
MARIADB_CFLAGS := -I$(MARIADB_CONNECTOR_DIR)/include -I$(MARIADB_CONNECTOR_DIR)/build/include
MARIADB_LIBS := $(MARIADB_CONNECTOR_ARCHIVE) -ldl -lm -lssl -lcrypto

$(MARIADB_CONNECTOR_ARCHIVE):
	$(MAKE) -C $(ROOT_DIR) mariadb-connector
