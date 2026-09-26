# MariaDB GTID Reader Design

## Goal

The binlog reader today snapshots MySQL `Executed_Gtid_Set` and streams
`GTID_LOG_EVENT` (`uuid:seq`). MariaDB GTIDs are `domain-server-sequence` and
use `GTID_EVENT`. This change makes the reader speak MariaDB while keeping the
existing `ST=` / `I1=` / `I2=` / `I3=` / `I4=` wire protocol so ProxySQL can
do causal reads against MariaDB.

ProxySQL-side routing lives in the ProxySQL repo. This spec is the reader
contract that repo depends on.

## Scope

In scope:

- Auto-detect MySQL vs MariaDB GTID strings.
- Snapshot File/Position without requiring MySQL's fifth column.
- Snapshot MariaDB position from `@@GLOBAL.gtid_binlog_pos`.
- Stream MariaDB `GTID_EVENT` as well as MySQL `GTID_LOG_EVENT`.
- Emit domain as the wire id (`I1=0:271`), sequence as trxid.
- MariaDB snapshot inserts watermark `[1, seq]` per domain.

Out of scope:

- New wire message types.
- A `--gtid-flavor` flag.
- Matching on `server_id` (display-only; ProxySQL keys on domain).
- Changing TLS, listener, or packaging behavior.

## Formats

MySQL (unchanged): UUID + `:` intervals, optional UUID dashes.

MariaDB: `domain-server-sequence` with three unsigned decimals, no `:`.
Sets are comma-separated, as in `@@gtid_binlog_pos` (`0-1-270,1-2-50`).
Sequence `0` is invalid. Leading zeros (`00-1-1`) are invalid. `domain` and
`server` are uint32 as in `GTID_EVENT`; anything wider than `UINT32_MAX` is
invalid rather than truncated.

Detection is per token: `:` → MySQL; `digits-digits-digits` → MariaDB;
anything else is invalid.

## Wire protocol

Unchanged text lines. MariaDB id is the domain, not `domain-server`:

```
ST=0:1-270,1:1-50
I1=0:271
I2=272
```

`ST=0:1-270` means domain 0 has executed sequences 1 through 270. A point
`ST=0:270` would make `has_gtid(0, 100)` false in ProxySQL, so snapshots must
emit the `[1, seq]` range.

## Snapshot

1. `SHOW BINARY LOG STATUS` or `SHOW MASTER STATUS` for File and Position.
   Two columns are enough; do not require five.
2. If a fifth column parses as a MySQL GTID set, use it.
3. Otherwise `SELECT @@GLOBAL.gtid_binlog_pos`. Parse as MariaDB. Empty or
   invalid fails startup.

MariaDB `SHOW MASTER STATUS` has no `Executed_Gtid_Set`. Current code that
requires `mysql_num_fields >= 5` and `row[4]` must not be the only path.

## Stream

Keep MySQL `GTID_LOG_EVENT` (UUID bytes + sequence).

Also handle MariaDB `GTID_EVENT`:

- `domain_id` and `sequence_nr` from the event body.
- `server_id` is not read. `GTID_EVENT` exposes it only in the event header,
   which the reader ignores; ProxySQL does not match on it, so streamed
   positions carry domain only and the display `server_id` stays whatever the
   snapshot text provided.
- Callback stays `(id, trxid)` with `id = decimal domain`.

A streamed sequence already present in the current position (inside the
snapshot watermark, or re-delivered after a reconnect) is a no-op: it updates
neither `last_trx_id`/`last_server_uuid` nor the queued `I1`/`I2` update,
because `ST=` already advertises it. This is what `GTID_Set::add()` returning
false means.

Non-GTID events stay ignored.

## GTID_Set and serialization

Reuse `GTID_Set`. MariaDB key is decimal `domain_id`. Snapshot `add` uses
`[1, seq]`. Incremental events `add` the single sequence.

Two representations exist, and they are not interchangeable:

- `to_string()` is the wire form. A 32-hex key serializes as a dashed UUID
  followed by `:intervals`; any other key (a MariaDB domain) serializes as
  `domain:start-end` using no `server_id` and no UUID dashes — `0:1-270`.
  `ST=` is `position_to_string(curpos)`, so `ST=` is this form.
- `to_display_string()` is the human/MariaDB-native form. A 32-hex key is
  left undashed; a domain key serializes as `domain-server-end` using the
  last-seen `server_id` or `0` — `0-1-270`. It is diagnostics only and is
  never sent on the wire.

`parse_mysql_gtid_executed` stays. Add `parse_mariadb_gtid_executed` and a
combined helper that tries MySQL then MariaDB.

## Error handling

- Invalid snapshot GTID set: fail startup, do not listen.
- Invalid MySQL fifth column must not silently fall through if it is present
  and non-empty but malformed; only missing/empty fifth column triggers the
  MariaDB `@@gtid_binlog_pos` fallback.
- Replication stream errors still stop the process as today.

## Testing

- Extend `test_mariadb_gtid-t` for MariaDB strings, watermark `[1, seq]`,
  rejects (leading zeros, `domain`/`server` above `UINT32_MAX`), wire
  `to_string()` vs display `to_display_string()`, and duplicate-`add`
  suppression. 49 assertions.
- Add a MariaDB 10.11 service to `test/infra` (separate from the dbdeployer
  MySQL matrix). Live TAP: snapshot, stream one committed transaction, observe
  `ST=` range then `I1=` domain:seq.
- Existing MySQL version matrix stays green.

## Verification

Parser TAP, MariaDB live TAP, full TAP build against the current MySQL
matrix, and the reader binary still serving MySQL `ST=`/`I1=` unchanged.
