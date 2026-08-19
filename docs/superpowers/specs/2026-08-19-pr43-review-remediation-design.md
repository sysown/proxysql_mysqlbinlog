# PR 43 Review Remediation Design

## Goal

Address the verified PR 43 review findings without changing the reader protocol
or weakening its TLS defaults.

## Reader correctness

`parse_mysql_gtid_executed()` will trim ASCII separator whitespace around each
comma-delimited UUID set before UUID and interval validation. Whitespace inside
a UUID or interval remains invalid. A TAP parser regression will cover the
space/newline form returned by MySQL for multi-source GTID sets.

The reader will use an atomic stop flag. The server event loop will initialize
an always-available libev async watcher before accepting replication updates.
On replication-stream failure, the reader will set the stop flag, asynchronously
break the server loop, join only a successfully created server thread, and exit
nonzero. The signal callback will lock `pos_mutex` before serializing `curpos`.

An integration TAP test will create an account without replication-stream
privileges. It proves a post-listener stream failure exits promptly and reports
failure instead of hanging on `pthread_join`.

## Delivery safeguards

CI will explicitly request `contents: read`. Release packaging will run the
same MariaDB/MySQL client dependency metadata check as CI before uploading
artifacts, and the Ruby workflow contract will enforce it.

Runtime Dockerfiles will collapse package installation and cache cleanup into a
single layer. Documentation will correct the Connector/C zlib statement and
label shell code blocks.

## Verification

Run focused GTID and shutdown TAP regressions, the complete TAP build, reader
CLI and workflow contracts, Dockerfile syntax checks, and the relevant
dbdeployer runner. The five-version matrix remains the final integration gate.
