# MariaDB GTID Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `proxysql_binlog_reader` snapshot and stream MariaDB GTIDs (`domain-server-seq`) on the existing `ST=`/`I1=`/`I2=` wire protocol.

**Architecture:** Auto-detect MySQL vs MariaDB GTID strings. Reuse `GTID_Set` with MariaDB key = decimal `domain_id` and watermark interval `[1, seq]`. Snapshot File/Position from `SHOW MASTER STATUS` (2 columns enough); GTID set from the executed-GTID column if present (auto-detected), else `@@GLOBAL.gtid_binlog_pos`. Stream both `GTID_LOG_EVENT` and MariaDB `GTID_EVENT`. `to_string()` stays wire format (`0:1-270`); `to_display_string()` is MariaDB `0-1-270` for humans.

**Tech Stack:** C++11, MariaDB Connector/C 3.4.8 replication API, TAP tests, docker-compose (`mariadb:10.11`).

**Worktree:** `/data/rene/proxysql2/proxysql_mysqlbinlog/.worktrees/mariadb-gtid` (branch `feature/mariadb-gtid`).

**Spec:** `docs/superpowers/specs/2026-09-23-mariadb-gtid-design.md`

---

## File map

- Modify: `mariadb_replication.h` / `mariadb_replication.cpp` — MariaDB parse + combined snapshot parse
- Modify: `proxysql_gtid.h` / `proxysql_gtid.cpp` — domain-key `to_string` / `to_display_string` / `last_server_id`
- Modify: `mariadb_replication_client.cpp` — snapshot fallback + executed-column auto-detection + `GTID_EVENT` + flavor-gated `@mariadb_slave_capability`
- Modify: `test/tap/tests/test_mariadb_gtid-t.cpp` — parser TAP
- Create: `test/tap/tests/test_mariadb_gtid_stream-t.cpp` — live MariaDB TAP
- Modify: `test/infra/docker-compose.yml` — MariaDB 10.11 service
- Modify: `test/tap/run.sh` — run MariaDB pass after MySQL matrix

Generic `%-t` already links `../libtap.a` (`mariadb_replication.cpp`, `proxysql_gtid.cpp`). No tests/Makefile change.

---

### Task 1: MariaDB GTID string parser

**Files:**
- Modify: `mariadb_replication.h`
- Modify: `mariadb_replication.cpp`
- Modify: `test/tap/tests/test_mariadb_gtid-t.cpp`

- [ ] **Step 1: Write the failing parser tests**

Build via `make -C test/tap` so `libtap.a` (already archives `mariadb_replication.cpp` and `proxysql_gtid.cpp`) is linked by the generic `%-t` rule. Do not add a second compile of those sources.

Append to `test_mariadb_gtid-t.cpp` (raise `plan()` by 12):

```cpp
	GTID_Set mdb;
	ok(parse_mariadb_gtid_executed("0-1-270", &mdb), "parse MariaDB single GTID");
	ok(mdb.has_gtid("0", 1) && mdb.has_gtid("0", 270) && !mdb.has_gtid("0", 271),
	   "MariaDB snapshot is watermark [1, seq]");
	ok(!mdb.has_gtid("1", 270), "other domain is absent");

	GTID_Set mdb_set;
	ok(parse_mariadb_gtid_executed("0-1-270,1-2-50", &mdb_set)
	       && mdb_set.has_gtid("0", 100) && mdb_set.has_gtid("1", 50),
	   "parse MariaDB multi-domain set");

	GTID_Set combined;
	ok(parse_gtid_executed("0-1-270", &combined) && combined.has_gtid("0", 270),
	   "combined parser accepts MariaDB");
	ok(parse_gtid_executed(
	       "24684d2a-9412-11ef-8c99-0242ac120002:1-3", &combined)
	       && combined.has_gtid("24684d2a941211ef8c990242ac120002", 3),
	   "combined parser still accepts MySQL");

	GTID_Set bad;
	ok(!parse_mariadb_gtid_executed("0-1", &bad), "reject two-field MariaDB");
	ok(!parse_mariadb_gtid_executed("0-1-0", &bad), "reject sequence 0");
	ok(!parse_mariadb_gtid_executed("00-1-1", &bad), "reject leading zeros");
	ok(!parse_mariadb_gtid_executed("0-1-1:2", &bad), "reject colon in MariaDB");
	ok(!parse_mariadb_gtid_executed("", &bad), "reject empty MariaDB set");
	ok(!parse_mariadb_gtid_executed("0-1-270,not-a-gtid", &bad),
	   "reject mixed junk");
```

Empty MySQL executed set is already valid (`parse_mysql_gtid_executed("", ...)` returns true). MariaDB empty is invalid because it means "no position", not "empty executed set".

- [ ] **Step 2: Run test to verify it fails**

Run: `make -C test/tap/tests test_mariadb_gtid-t && ./test/tap/tests/test_mariadb_gtid-t`

Expected: compile error (`parse_mariadb_gtid_executed` undeclared) or FAIL.

- [ ] **Step 3: Implement parser**

`mariadb_replication.h`:

```cpp
bool parse_mariadb_gtid_executed(const std::string& encoded, GTID_Set* out);
bool parse_gtid_executed(const std::string& encoded, GTID_Set* out);
```

`parse_mariadb_gtid_executed` in `mariadb_replication.cpp`:

- Split on commas; trim separator whitespace (reuse `trim_separator_whitespace`).
- Each token: exactly two `-`, three unsigned decimal fields, no leading zeros (except the number `0`), sequence `> 0`.
- `out->add(domain_str, trxid_t(1), seq)` (watermark).
- Do not call `set_server_id` yet (Task 2).

`parse_gtid_executed`: if `encoded.find(':') != npos` use MySQL parser; else MariaDB parser.

- [ ] **Step 4: Run test to verify it passes**

Run: `./test/tap/tests/test_mariadb_gtid-t`

Expected: all assertions PASS.

- [ ] **Step 5: Commit**

```bash
git add mariadb_replication.h mariadb_replication.cpp \
  test/tap/tests/test_mariadb_gtid-t.cpp test/tap/tests/Makefile
git commit -m "feat: parse MariaDB domain-server-seq GTID sets"
```

---

### Task 2: Domain-key serialization

**Files:**
- Modify: `proxysql_gtid.h`
- Modify: `proxysql_gtid.cpp`
- Modify: `test/tap/tests/test_mariadb_gtid-t.cpp`

Wire `ST=` is `position_to_string(curpos)` → `GTID_Set::to_string()`. UUID `insert(8,"-")` throws `out_of_range` for key `"0"`. Stats/display want `0-1-270`. Split the two:

- `to_string()` = wire: 32-hex → dashed UUID + `:` intervals; else `id:start-end` (`0:1-270`)
- `to_display_string()` = 32-hex unchanged; domain key → `domain-server-end` using `last_server_id` or `0`

- [ ] **Step 1: Write failing serialization tests**

```cpp
	GTID_Set wire;
	parse_mariadb_gtid_executed("0-1-270", &wire);
	ok(wire.to_string() == "0:1-270", "wire to_string is domain:1-seq");
	ok(wire.to_display_string() == "0-1-270",
	   "display string keeps MariaDB native form");
	wire.add("0", trxid_t(271));
	ok(wire.to_string() == "0:1-271", "incremental seq extends watermark");
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compile error (`to_display_string` / `set_server_id` missing) or `out_of_range`.

- [ ] **Step 3: Implement**

`proxysql_gtid.h` add to `GTID_Set`:

```cpp
		std::unordered_map<std::string, uint32_t> last_server_id;
		void set_server_id(const std::string& id, uint32_t server_id);
		uint32_t get_server_id(const std::string& id) const;
		const std::string to_display_string(void);
```

`to_string()`: if `it->first.size()==32` keep current dash insertion; else `out << it->first` then `:` intervals (no dashes).

`to_display_string()`: 32-hex keys same as `to_string()`; domain keys emit `id + "-" + server + "-" + max_end` comma-separated.

`copy()` copies `last_server_id`. `clear()` clears it.

`parse_mariadb_gtid_executed` calls `set_server_id`.

- [ ] **Step 4: Run tests**

Expected: PASS, including existing MySQL `to_string` cases in this binary.

- [ ] **Step 5: Commit**

```bash
git add proxysql_gtid.h proxysql_gtid.cpp mariadb_replication.cpp \
  test/tap/tests/test_mariadb_gtid-t.cpp
git commit -m "feat: serialize MariaDB GTID domain keys without UUID dashes"
```

---

### Task 3: Snapshot fallback

**Files:**
- Modify: `mariadb_replication_client.cpp`
- Modify: `test/tap/tests/test_mariadb_gtid-t.cpp` (snapshot-position tests stay; add a pure helper test if snapshot parse is extracted)

Extract the GTID-set choice so it is unit-testable without a server:

```cpp
bool snapshot_gtid_set(const char* executed_gtid_set_or_null,
                       const std::string& mariadb_binlog_pos,
                       GTID_Set* out);
```

Rules (spec):

1. If `executed_gtid_set_or_null` is non-null and non-empty: parse as MySQL or fail (no MariaDB fallback).
2. Else parse `mariadb_binlog_pos` as MariaDB or fail.

- [ ] **Step 1: Failing tests**

```cpp
	GTID_Set s;
	ok(snapshot_gtid_set("24684d2a-9412-11ef-8c99-0242ac120002:1-3", "", &s)
	       && s.has_gtid("24684d2a941211ef8c990242ac120002", 3),
	   "non-empty MySQL fifth column wins");
	ok(!snapshot_gtid_set("not-a-gtid", "0-1-270", &s),
	   "malformed MySQL fifth column does not fall through");
	ok(snapshot_gtid_set(nullptr, "0-1-270", &s) && s.has_gtid("0", 270),
	   "missing fifth column uses MariaDB binlog pos");
	ok(snapshot_gtid_set("", "0-1-270", &s) && s.map.empty(),
	   "empty fifth column is empty MySQL set");
	ok(!snapshot_gtid_set(nullptr, "", &s), "missing both fails");
```

- [ ] **Step 2: Run — expect FAIL**

- [ ] **Step 3: Implement helper + use it in `MariaDBReplicationClient::snapshot()`**

`snapshot()`:

- `SHOW BINARY LOG STATUS` then `SHOW MASTER STATUS`.
- Require `mysql_num_fields >= 2` and `row[0]`, `row[1]` (File, Position). Stop requiring 5 columns / `row[4]`.
- `const char* fifth = (nfields >= 5) ? row[4] : nullptr;`
- If fifth is missing/empty: `SELECT @@GLOBAL.gtid_binlog_pos` into a string.
- `snapshot_gtid_set(fifth, mariadb_pos, &set)` or throw.

- [ ] **Step 4: Run parser TAP — PASS**

- [ ] **Step 5: Commit**

```bash
git add mariadb_replication.h mariadb_replication.cpp \
  mariadb_replication_client.cpp test/tap/tests/test_mariadb_gtid-t.cpp
git commit -m "feat: snapshot MariaDB GTID via @@gtid_binlog_pos"
```

- [ ] **Step 6: Auto-detect a present executed-GTID column (revision)**

`snapshot_gtid_set()` was written assuming the executed column is always
MySQL-shaped. That assumption is not safe: any MariaDB flavor that exposes
`Executed_Gtid_Set` reports it in the native `domain-server-sequence` form,
which `parse_mysql_gtid_executed()` rejects — the snapshot would fail on a
server that is otherwise supported. Use the auto-detecting helper instead, and
keep `snapshot_gtid_set()` for the missing-column fallback only:

```cpp
	if (nfields >= 5) {
		const char* fifth = row[4] ? row[4] : "";
		if (*fifth != '\0' && !parse_gtid_executed(fifth, &set))
			throw std::runtime_error(...);
	} else {
		... SELECT @@GLOBAL.gtid_binlog_pos ...
		snapshot_gtid_set(nullptr, mariadb_pos, &set) or throw;
	}
```

An empty column stays an empty set: it is ambiguous (no GTIDs executed, e.g.
MySQL with `gtid_mode=OFF`) and must not fail the snapshot.

Verified: MySQL 8.4 (`SHOW BINARY LOG STATUS` has 5 columns, MySQL form) and
MariaDB 10.11 (4 columns, fallback path) both snapshot and stream.

---

### Task 4: Stream MariaDB `GTID_EVENT`

**Files:**
- Modify: `mariadb_replication_client.cpp`

Connector/C (`mariadb_rpl.h`):

```c
#define GTID_EVENT 162
struct st_mariadb_rpl_gtid_event {
  uint64_t sequence_nr;
  uint32_t domain_id;
  ...
};
```

Event union field: `event->event.gtid`. `server_id` is not read from the event header: ProxySQL does not match on it, so the streamed id is the domain alone and `last_server_id` keeps whatever the snapshot text provided (see Task 7).

- [ ] **Step 1: Extend `stream_events` (no live server in this task)**

Keep `GTID_LOG_EVENT`. Add:

```cpp
		if (event->event_type == GTID_EVENT && on_gtid) {
			const uint32_t domain = event->event.gtid.domain_id;
			const uint64_t seq = event->event.gtid.sequence_nr;
			on_gtid(std::to_string(domain), seq);
		}
```

Callback signature stays `(id, trxid)`. Domain id is decimal text (`"0"`).

There is no unit fixture of raw binlog bytes in this repo; live coverage is Task 5. After this change, existing MySQL TAP must still see only `GTID_LOG_EVENT`.

- [ ] **Step 1b: Gate the MariaDB handshake hint on the server flavor (revision)**

`open_stream()` sent `SET @mariadb_slave_capability=4` unconditionally. It is
a MariaDB-only user variable, so on MySQL the statement is a wasted round trip
before every stream. Gate it on the connected banner, extracted as a pure
helper so it is unit-testable:

```cpp
/** Whether a server_version banner identifies a MariaDB server. */
bool is_mariadb_server(const char* server_version);
```

```cpp
	ok(is_mariadb_server("10.11.18-MariaDB-ubu2204-log"),
	   "flavor detect accepts a MariaDB version banner");
	ok(is_mariadb_server("5.5.5-10.11.18-MariaDB"),
	   "flavor detect accepts a replication-prefixed MariaDB banner");
	ok(!is_mariadb_server("8.0.36"), "flavor detect rejects MySQL");
	ok(!is_mariadb_server(nullptr), "flavor detect rejects a null banner");
```

```cpp
	if (is_mariadb_server(impl_->mysql->server_version)
		&& mysql_query(impl_->mysql, "SET @mariadb_slave_capability=4"))
		throw connector_error("cannot set MariaDB replica capability", impl_->mysql);
```

Verified: MySQL 8.4 streams without the statement (`test_basic_updates-t`),
MariaDB 10.11 still sends it (`test_mariadb_gtid_stream-t`).

- [ ] **Step 2: Commit**

```bash
git add mariadb_replication_client.cpp
git commit -m "feat: stream MariaDB GTID_EVENT as domain:sequence"
```

---

### Task 5: MariaDB 10.11 infra + live TAP

**Files:**
- Modify: `test/infra/docker-compose.yml`
- Modify: `test/tap/run.sh`
- Create: `test/tap/tests/test_mariadb_gtid_stream-t.cpp`

- [ ] **Step 1: Add compose service**

```yaml
  mariadb:
    image: mariadb:10.11
    container_name: binlog-reader-mariadb
    restart: always
    environment:
      MARIADB_ROOT_PASSWORD: root
    command: >
      --log-bin=mysql-bin
      --server-id=11
      --binlog-format=ROW
      --gtid-strict-mode=1
      --bind-address=0.0.0.0
    ports:
      - "3311:3306"
    healthcheck:
      test: ["CMD", "healthcheck.sh", "--connect", "--innodb_initialized"]
      interval: 5s
      timeout: 5s
      retries: 30
      start_period: 30s
```

Grant is default root. Reader needs `REPLICATION SLAVE` / `REPLICATION CLIENT` (root has them).

- [ ] **Step 2: Write live TAP `test_mariadb_gtid_stream-t.cpp`**

Skip unless `VERSION()` contains `MariaDB`:

```cpp
int main() {
	plan(3);
	CommandLine cli;
	MySQLClient db;
	if (!db.connect(cli))
		BAIL_OUT("cannot connect: %s", db.last_error().c_str());
	if (mysql_query(db.raw(), "SELECT VERSION()"))
		BAIL_OUT("SELECT VERSION failed: %s", mysql_error(db.raw()));
	MYSQL_RES* ver_res = mysql_store_result(db.raw());
	MYSQL_ROW ver_row = ver_res ? mysql_fetch_row(ver_res) : nullptr;
	std::string ver = (ver_row && ver_row[0]) ? ver_row[0] : "";
	mysql_free_result(ver_res);
	if (ver.find("MariaDB") == std::string::npos) {
		skip(3, "not MariaDB (version=%s)", ver.c_str());
		return exit_status();
	}
	db.exec("CREATE DATABASE IF NOT EXISTS binlog_reader_test");
	db.exec("CREATE TABLE IF NOT EXISTS binlog_reader_test.mdb_gtid_t "
	        "(id INT PRIMARY KEY AUTO_INCREMENT, v INT)");
	BinlogReaderProcess reader;
	auto reader_host = setup_reader(cli, reader);
	if (reader_host.empty())
		BAIL_OUT("failed to start reader");
	BinlogReaderClient client;
	if (!client.connect(reader_host, cli.reader_port, 2000))
		BAIL_OUT("cannot connect to reader");
	BinlogReaderMsg st = client.read_line(10000);
	ok(st.valid() && st.kind == "ST" && st.uuid == "0" && !st.intervals.empty()
	       && st.intervals[0].start == 1,
	   "MariaDB ST= is domain 0 watermark (raw='%s')", st.raw.c_str());
	if (!db.exec("INSERT INTO binlog_reader_test.mdb_gtid_t (v) VALUES (1)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	BinlogReaderMsg m1 = client.read_line(5000);
	ok(m1.valid() && m1.kind == "I1" && m1.uuid == "0" &&
	       m1.intervals.size() == 1 && m1.intervals[0].start > 0,
	   "INSERT emits I1=0:<seq> (raw='%s')", m1.raw.c_str());
	if (!db.exec("INSERT INTO binlog_reader_test.mdb_gtid_t (v) VALUES (2)"))
		BAIL_OUT("INSERT failed: %s", db.last_error().c_str());
	BinlogReaderMsg m2 = client.read_line(5000);
	ok(m2.valid() && m2.kind == "I2" &&
	       m2.intervals.size() == 1 &&
	       m2.intervals[0].start == m1.intervals[0].start + 1,
	   "second INSERT emits I2=<seq+1> (raw='%s')", m2.raw.c_str());
	return exit_status();
}
```

No Makefile change: generic `%-t` links `../libtap.a`, which already contains `binlog_reader_client.cpp`, `binlog_reader_process.cpp`, `mysql_client.cpp`, `mariadb_replication.cpp`, and `proxysql_gtid.cpp`.

- [ ] **Step 3: run.sh MariaDB pass**

After the MySQL version loop, if port 3311 is reachable (or `MARIADB_PORT` set):

```bash
export MYSQL_VERSION=mdb11
export MYSQL_PORT=${MARIADB_PORT:-3311}
# run only test_mariadb_gtid_stream-t (other tests skip, but keep the skip path)
```

Also run `test_mariadb_gtid-t` once outside the version loop if it is currently re-run 5 times — optional, not required.

- [ ] **Step 4: Run**

Parser: `./test/tap/tests/test_mariadb_gtid-t` PASS.

Live: start compose mariadb, `MYSQL_PORT=3311 MYSQL_VERSION=mdb11 ./test/tap/tests/test_mariadb_gtid_stream-t` PASS.

MySQL `test_basic_updates-t` still PASS.

- [ ] **Step 5: Commit**

```bash
git add test/infra/docker-compose.yml test/tap/run.sh \
  test/tap/tests/test_mariadb_gtid_stream-t.cpp test/tap/tests/Makefile
git commit -m "test: live MariaDB GTID snapshot and stream TAP"
```

---

### Task 6: Verification

- [ ] `make -C test/tap/tests` — full TAP build
- [ ] `./test/tap/tests/test_mariadb_gtid-t` — parser
- [ ] Existing MySQL TAP unchanged (`test_basic_updates-t`, `test_snapshot_startup-t`)
- [ ] Live MariaDB TAP when compose MariaDB is healthy

---

### Task 7: PR 49 review remediation

Automated-review findings on the branch, all verified against the code before
fixing. `test_mariadb_gtid-t` goes from `plan(42)` to `plan(49)`.

- [x] **Reject `domain`/`server` above `UINT32_MAX`** — the snapshot text comes
      from a server as decimal text, but `domain_id` and `server_id` are uint32
      in `GTID_EVENT`; `strtoul` + cast truncated a 33-bit value silently, and
      a 33-bit domain would also reach ProxySQL as a `ST=` id it cannot parse
      back. `parse_uint32_field()` in `mariadb_replication.cpp` now bounds both
      before the watermark `add` and the `set_server_id` call. New tests:
      `0-4294967296-1` and `4294967296-1-1` rejected, no partial state left,
      and `4294967295-4294967295-1` still accepted.

- [x] **Honor `GTID_Set::add()` in `bench_gtid_callback()`** — a false return
      means the sequence is already in `curpos` (snapshot watermark or a
      re-delivered GTID), so `last_trx_id`/`last_server_uuid` and the queued
      `I1`/`I2` are left alone: `ST=` already advertises it. Characterization
      tests pin the `add()` contract the callback now depends on. The reader
      architecture is unchanged.

- [x] **Spec/plan serialization wording** — the spec conflated wire and display
      forms. `to_string()` is `domain:start-end` (`0:1-270`) and is what `ST=`
      uses; `to_display_string()` is `domain-server-end` (`0-1-270`) and is
      never sent. The stale "server_id from the event header" claim is gone
      from both documents.

- [x] **`test/tap/run.sh` MariaDB probe** — the probe used `MYSQL_HOST:3311`
      while the tests ran against `${MARIADB_HOST:-$MYSQL_HOST}:${MARIADB_PORT:-3311}`,
      so a custom `MARIADB_HOST` without `MARIADB_PORT` was skipped. Host and
      port are now resolved once, before the probe.

---

## Spec coverage

| Spec item | Task |
| --- | --- |
| Auto-detect `:` vs `digits-digits-digits` | 1 |
| Watermark `[1, seq]` | 1 |
| Reject leading zeros / seq 0 | 1 |
| Wire `ST=0:1-270` | 2 |
| Display `0-1-270` | 2 |
| SHOW MASTER STATUS 2 columns | 3 |
| Malformed MySQL fifth column does not fall through | 3 |
| Present executed column auto-detected, not forced to MySQL | 3 |
| `@@gtid_binlog_pos` | 3 |
| `GTID_EVENT` stream | 4 |
| `@mariadb_slave_capability` only on MariaDB | 4 |
| Live MariaDB TAP + 10.11 service | 5 |
| MySQL matrix unchanged | 6 |
| Reject `domain`/`server` above `UINT32_MAX` | 7 |
| Duplicate GTID emits no incremental update | 7 |
| Wire vs display serialization in spec | 7 |
| MariaDB probe host/port matches the tests | 7 |
