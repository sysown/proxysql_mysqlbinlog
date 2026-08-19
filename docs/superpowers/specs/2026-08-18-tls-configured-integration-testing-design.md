# TLS-Configured Integration Testing Design

## Goal

Keep the real dbdeployer MySQL integration matrix for MySQL 5.7, 8.0, 8.4,
9.0, and 9.4, while giving the binlog reader explicit, secure TLS connection
controls. Each CI test must start a real server and have the reader connect to
it and stream binlog events.

## Reader TLS Interface

The reader will accept these long command-line options:

- `--ssl-mode=DISABLED|PREFERRED|REQUIRED` controls transport security and
  defaults to `REQUIRED`.
- `--ssl-verify-server-cert=0|1` controls peer and hostname verification and
  defaults to `1`.
- `--ssl-ca`, `--ssl-capath`, `--ssl-cert`, `--ssl-key`, `--ssl-cipher`, and
  `--tls-version` pass the corresponding TLS material or constraint to
  MariaDB Connector/C.

`DISABLED` is the only mode that requests a plaintext connection. `PREFERRED`
may fall back to plaintext only when server verification has been explicitly
disabled; a failed trust or hostname check is never downgraded to plaintext.
`REQUIRED` fails unless a TLS connection is established. Verification is
independently controlled so an operator can retain encrypted transport for a
private, self-signed server by explicitly setting
`--ssl-verify-server-cert=0`.

The interface deliberately does not advertise MySQL's `VERIFY_CA` and
`VERIFY_IDENTITY` ssl-mode names. Connector/C 3.4.8 exposes one verification
switch; in the vendored OpenSSL build it validates both certificate trust and
the target hostname. Two names would falsely imply separately configurable
semantics.

## Connector/C Mapping

`MariaDBConnectionOptions` owns the parsed TLS values. Before
`mysql_real_connect`, `MariaDBReplicationClient` applies the options through
Connector/C: `MYSQL_OPT_SSL_ENFORCE`,
`MYSQL_OPT_SSL_VERIFY_SERVER_CERT`, and the matching CA, client certificate,
key, cipher, and TLS-version options. An invalid mode, invalid boolean, or
failed Connector/C option call is a startup error with a clear diagnostic.

The TAP SQL helper uses the same option model. This ensures setup queries and
the reader have consistent connection behavior during integration tests.

## Runtime Images

Every active runtime image forwards these variables to the reader:

- `SSL_MODE`, defaulting to `REQUIRED`
- `SSL_VERIFY_SERVER_CERT`, defaulting to `1`
- `SSL_CA`, `SSL_CAPATH`, `SSL_CERT`, `SSL_KEY`, `SSL_CIPHER`, and
  `TLS_VERSION` when supplied

This leaves production secure without requiring an entrypoint wrapper or
embedding certificates in the image.

## dbdeployer Test Matrix

The existing dbdeployer infrastructure remains the source of truth for the
five MySQL versions. A CI matrix entry brings up exactly one sandbox, waits
for its healthcheck, then runs the TAP suite against the sandbox over the
compose network. Tests set `SSL_MODE=REQUIRED` and
`SSL_VERIFY_SERVER_CERT=0`, preserving TLS encryption while intentionally
accepting dbdeployer's self-signed certificate.

A dedicated TLS TAP regression has two phases against every supported server:

1. The default reader configuration must reject the self-signed certificate
   and must not publish a listener.
2. The explicit verification opt-out must start successfully, publish an
   `ST` message, and deliver a subsequent binlog `I` message after an insert.

The existing cold `caching_sha2_password` regression continues to run on
versions that provide that plugin. Logs remain separated by run ID, MySQL
version, test binary, and reader process so a failed connection is diagnosable
from the CI artifact.

## Non-Goals

This change does not create a private CA for dbdeployer, change the selected
server versions, alter legacy Docker targets, or modify consumer-facing binlog
protocol messages.
