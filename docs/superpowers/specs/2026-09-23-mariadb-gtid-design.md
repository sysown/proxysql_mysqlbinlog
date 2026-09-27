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
Sequence `0` is invalid. Leading zeros (`00-1-1`) are invalid.

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

1. `SHOW BINARY LOG STATUS` or `SHOW MASTER STATUS` supplies File and Position.
   Two columns are enough; do not require five, and do not reject the query
   because it returned fewer than five.
2. When a fifth column is present and non-empty, it is the snapshot GTID set.
   Parse it with the combined auto-detecting parser, which tries MySQL first
   and then MariaDB, so a MySQL `Executed_Gtid_Set` and a MariaDB position
   text are both accepted. Malformed non-empty text fails startup; it does
   not silently fall through to the MariaDB fallback.
3. Only when the fifth column is missing, or present but empty, run
   `SELECT @@GLOBAL.gtid_binlog_pos` and parse it as MariaDB. An empty value
   is a valid empty GTID_Set, not an error: it yields an empty `GTID_Set` and
   the reader waits for the first non-empty position instead of failing
   startup. Only malformed non-empty text fails startup. The reader's main
   loop waits for the first non-empty position before opening the stream, so
   an empty snapshot never starts streaming from a bogus position.

MariaDB `SHOW MASTER STATUS` has no `Executed_Gtid_Set`. Current code that
requires `mysql_num_fields >= 5` and `row[4]` must not be the only path.

## Stream

Keep MySQL `GTID_LOG_EVENT` (UUID bytes + sequence).

Also handle MariaDB `GTID_EVENT`:

- `domain_id` and `sequence_nr` from the event body.
- `server_id` from the event header (not sent on the wire; ProxySQL does not
  match on it).
- Callback stays `(id, trxid)` with `id = decimal domain`.

Non-GTID events stay ignored.

## GTID_Set and to_string

Reuse `GTID_Set`. MariaDB key is decimal `domain_id`. Snapshot `add` uses
`[1, seq]`. Incremental events `add` the single sequence.

`to_string()` must not insert UUID dashes into domain keys. Domain keys
serialize as `domain-server-end` using last-seen `server_id` or `0`.

`parse_mysql_gtid_executed` stays. Add `parse_mariadb_gtid_executed` and a
combined helper that tries MySQL then MariaDB.

## Error handling

- Malformed non-empty snapshot GTID set: fail startup, do not listen. An empty
  snapshot (`@@gtid_binlog_pos` = `''`) is accepted and produces an empty
  `GTID_Set`; the reader waits for the first non-empty position.
- Invalid MySQL fifth column must not silently fall through if it is present
  and non-empty but malformed; only missing/empty fifth column triggers the
  MariaDB `@@gtid_binlog_pos` fallback.
- Replication stream errors still stop the process as today.

## Testing

- Extend `test_mariadb_gtid-t` for MariaDB strings, watermark `[1, seq]`,
  rejects (including leading zeros), and `to_string` without UUID dashes.
- Add a MariaDB 10.11 service to `test/infra` (separate from the dbdeployer
  MySQL matrix). Live TAP: snapshot, stream one committed transaction, observe
  `ST=` range then `I1=` domain:seq.
- Existing MySQL version matrix stays green.

## Verification

Parser TAP, MariaDB live TAP, full TAP build against the current MySQL
matrix, and the reader binary still serving MySQL `ST=`/`I1=` unchanged.
