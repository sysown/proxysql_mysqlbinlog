# Vendored MariaDB Connector/C Design

## Goal

Ship the replication reader with MariaDB Connector/C 3.4.8 linked statically.
The binary must support MySQL `caching_sha2_password` without requiring a
MariaDB client package or a Connector/C plugin directory at runtime.

## Source and licensing

Commit the upstream `mariadb-connector-c-3.4.8.tar.gz` source archive beside
the existing vendored archives. Record its upstream tag commit and SHA-256 in a
small manifest or Makefile variables. Preserve the archive's LGPL-2.1-or-later
licence text and add a third-party notice describing the static linkage.

## Build design

The top-level Makefile will unpack the archive into an ignored directory and
use CMake to build `libmariadbclient.a`. The Connector/C configuration sets
`CLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC`, embedding the authentication
plugin in the archive. The reader links that archive directly rather than
calling `mariadb_config`; Connector/C's required system libraries, including
OpenSSL and zlib, remain normal platform-provided dependencies.

Build images install generic development tools and headers only. They no longer
install a distribution Connector/C development package.

## Packaging and runtime

Debian and RPM metadata no longer declare a MariaDB Connector/C runtime
dependency. Runtime Dockerfiles likewise remove `libmariadb3` and
`mariadb-connector-c`. The package continues to contain only the reader binary;
no separately loadable Connector/C plugins are shipped.

## Verification

CI builds the reader and packages from the vendored archive for every active
distribution target. A link check verifies that `ldd proxysql_binlog_reader`
does not reference `libmariadb`, `libmariadbclient`, or `libmysqlclient`. The
existing MySQL 5.7, 8.0, 8.4, 9.0, and 9.4 matrix runs the TAP suite, including
the cold `caching_sha2_password` regression. Package inspection confirms no
Connector/C runtime dependency is emitted.

## Non-goals

This change does not fully statically link libc, OpenSSL, or other operating
system libraries. Legacy `docker/build/OLD` images remain outside this initial
migration.
