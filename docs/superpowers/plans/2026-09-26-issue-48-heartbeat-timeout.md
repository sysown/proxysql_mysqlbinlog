# Issue 48 Heartbeat and Read Timeout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `proxysql_binlog_reader` detect a silent replication partition instead of serving a frozen GTID set forever.

**Architecture:** Add positive `--heartbeat-period` and `--read-timeout` options. The reader passes the read timeout to Connector/C before connecting and asks MySQL for nanosecond heartbeats before opening the binlog stream. Connector/C already returns heartbeat events; the existing event loop ignores them. A dead link therefore makes `mariadb_rpl_fetch()` fail, and the existing supervisor loop exits/restarts.

**Tech Stack:** C++11, MariaDB Connector/C 3.4.8, libdaemon supervisor, TAP tests.

**Worktree:** `/data/rene/proxysql2/proxysql_mysqlbinlog/.worktrees/issue-48-heartbeat` (branch `fix/issue-48-heartbeat-timeout`).

**Issue:** https://github.com/sysown/proxysql_mysqlbinlog/issues/48

**Defaults:** heartbeat period 5 seconds; read timeout 60 seconds. Both must be positive. Read timeout must be at least three heartbeat periods so an idle source does not trip the socket timeout between heartbeats.

---

## File map

- Modify: `mariadb_replication_client.h` — connection option fields
- Modify: `mariadb_replication_client.cpp` — validate/set read timeout and heartbeat
- Modify: `proxysql_binlog_reader.cpp` — CLI options, usage, connection wiring
- Modify: `test/tap/binlog_reader_process.h/.cpp` — pass new reader flags
- Modify: `test/tap/tests/Makefile` — link the replication client into the pure-helper test
- Modify: `test/tap/tests/test_basic_startup-t.cpp` — smoke-test explicit options
- Modify: `test/tap/tests/test_replication_failure_shutdown-t.cpp` — verify failure path still logs/exits
- Modify: `README.md` — document options and defaults

---

### Task 1: Add timeout/heartbeat fields and pure validation

**Files:**
- Modify: `mariadb_replication_client.h`
- Modify: `mariadb_replication_client.cpp`
- Create: `test/tap/tests/test_replication_timeouts-t.cpp`
- Modify: `test/tap/tests/Makefile` — add a rule linking `mariadb_replication_client.cpp` and `mariadb_replication.cpp`

- [ ] **Step 1: Write the failing validation test**

Add a public pure helper in `mariadb_replication_client.h`:

```cpp
bool validate_replication_timeouts(unsigned int heartbeat_period_seconds,
                                   unsigned int read_timeout_seconds);
```

The test must cover:

```cpp
	ok(validate_replication_timeouts(5, 60), "defaults are valid");
	ok(validate_replication_timeouts(1, 3), "three heartbeat periods are valid");
	ok(!validate_replication_timeouts(0, 60), "zero heartbeat is rejected");
	ok(!validate_replication_timeouts(5, 0), "zero read timeout is rejected");
	ok(!validate_replication_timeouts(5, 14), "timeout below three periods is rejected");
	ok(!validate_replication_timeouts(0, 0), "both zero is rejected");
```

Add an explicit rule in `test/tap/tests/Makefile` (the generic `%-t` rule only links `libtap.a`, which does not archive `mariadb_replication_client.cpp`):

```makefile
test_replication_timeouts-t: test_replication_timeouts-t.cpp \
		../../../mariadb_replication_client.cpp ../../../mariadb_replication.cpp \
		../../../tls_options.cpp ../../../proxysql_gtid.cpp \
		../tap.cpp $(MARIADB_CONNECTOR_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(MARIADB_CFLAGS) -I.. -I../../.. \
		test_replication_timeouts-t.cpp ../../../mariadb_replication_client.cpp \
		../../../mariadb_replication.cpp ../../../tls_options.cpp \
		../../../proxysql_gtid.cpp ../tap.cpp \
		$(MARIADB_LIBS) -lpthread -o $@
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C test/tap` and `./test/tap/tests/test_replication_timeouts-t`
Expected: compile failure because the helper does not exist.

- [ ] **Step 3: Implement validation**

```cpp
bool validate_replication_timeouts(unsigned int heartbeat_period_seconds,
                                   unsigned int read_timeout_seconds) {
	if (heartbeat_period_seconds == 0 || read_timeout_seconds == 0)
		return false;
	return read_timeout_seconds >= heartbeat_period_seconds * 3U;
}
```

Use overflow-safe multiplication (`heartbeat_period_seconds > UINT_MAX / 3` rejects).

- [ ] **Step 4: Run the test to verify it passes**

Expected: all six assertions pass.

- [ ] **Step 5: Commit**

```bash
git add mariadb_replication_client.h mariadb_replication_client.cpp \
  test/tap/tests/Makefile test/tap/tests/test_replication_timeouts-t.cpp
git commit -m "fix: validate replication heartbeat and read timeouts"
```

---

### Task 2: Wire Connector/C read timeout and replication heartbeat

**Files:**
- Modify: `mariadb_replication_client.h`
- Modify: `mariadb_replication_client.cpp`
- Modify: `test/tap/tests/test_replication_timeouts-t.cpp`

- [ ] **Step 1: Extend the connection options**

```cpp
struct MariaDBConnectionOptions {
	std::string host;
	unsigned int port;
	std::string user;
	std::string password;
	TLSOptions tls;
	unsigned int heartbeat_period_seconds = 5;
	unsigned int read_timeout_seconds = 60;
};
```

- [ ] **Step 2: Add failing tests for the heartbeat SQL builder**

Extract a pure helper for the exact statement so it can be tested without a server:

```cpp
std::string heartbeat_statement(unsigned int heartbeat_period_seconds);
```

Expected exact output for `5`: `SET @master_heartbeat_period = 5000000000`.

- [ ] **Step 3: Run the tests to verify the helper is missing**

Expected: compile failure.

- [ ] **Step 4: Implement the helper and `connect()` wiring**

Before `mysql_real_connect()`:

```cpp
	const unsigned int read_timeout = impl_->options.read_timeout_seconds;
	if (mysql_options(impl_->mysql, MYSQL_OPT_READ_TIMEOUT, &read_timeout))
		throw std::runtime_error("cannot set MySQL read timeout");
```

Reject invalid options before `mysql_init()` with a clear `std::runtime_error`.

In `open_stream()`, before `mariadb_rpl_init()`:

```cpp
	const std::string heartbeat = heartbeat_statement(
		impl_->options.heartbeat_period_seconds);
	if (mysql_query(impl_->mysql, heartbeat.c_str()))
		throw connector_error("cannot enable replication heartbeats", impl_->mysql);
```

`heartbeat_statement()` must compute `seconds * 1,000,000,000ULL` with overflow rejection.

- [ ] **Step 5: Run focused tests**

```bash
make -C test/tap
./test/tap/tests/test_replication_timeouts-t
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add mariadb_replication_client.h mariadb_replication_client.cpp \
  test/tap/tests/test_replication_timeouts-t.cpp
git commit -m "fix: enable replication heartbeats and read timeout"
```

---

### Task 3: Add CLI flags and usage text

**Files:**
- Modify: `proxysql_binlog_reader.cpp`
- Modify: `README.md`
- Modify: `test/tap/binlog_reader_process.h`
- Modify: `test/tap/binlog_reader_process.cpp`

- [ ] **Step 1: Add process-wrapper fields**

```cpp
	int heartbeat_period_seconds = -1;
	int read_timeout_seconds = -1;
```

In `start()`, append:

```cpp
	if (heartbeat_period_seconds >= 0) {
		argv.push_back("--heartbeat-period");
		argv.push_back(std::to_string(heartbeat_period_seconds));
	}
	if (read_timeout_seconds >= 0) {
		argv.push_back("--read-timeout");
		argv.push_back(std::to_string(read_timeout_seconds));
	}
```

- [ ] **Step 2: Add long options and parsing**

Add enum values after the TLS options and long-option entries:

```cpp
	OPTION_HEARTBEAT_PERIOD,
	OPTION_READ_TIMEOUT,
```

```cpp
	{"heartbeat-period", required_argument, nullptr, OPTION_HEARTBEAT_PERIOD},
	{"read-timeout", required_argument, nullptr, OPTION_READ_TIMEOUT},
```

Parse with `std::stoul`, reject malformed values and zero, then validate the pair with `validate_replication_timeouts()`. Print the error, usage, and return 1 on invalid input.

- [ ] **Step 3: Update usage text**

Document:

```
	"--heartbeat-period: Replication heartbeat period in seconds (default 5).\n"
	"--read-timeout: Replication read timeout in seconds (default 60).\n"
```

Pass the parsed values into `MariaDBConnectionOptions`.

- [ ] **Step 4: Update README**

Document both options, their defaults, the 3x relationship, and that a timeout/stream error causes the existing supervisor to restart the reader.

- [ ] **Step 5: Build and run existing TAP**

```bash
make
make -C test/tap
./test/tap/tests/test_listener_options-t
```

Expected: build succeeds and existing listener/TLS tests remain green.

- [ ] **Step 6: Commit**

```bash
git add proxysql_binlog_reader.cpp README.md \
  test/tap/binlog_reader_process.h test/tap/binlog_reader_process.cpp
git commit -m "feat: configure replication heartbeat and read timeout"
```

---

### Task 4: TAP smoke coverage for explicit options

**Files:**
- Modify: `test/tap/tests/test_basic_startup-t.cpp`
- Modify: `test/tap/tests/test_replication_failure_shutdown-t.cpp`

- [ ] **Step 1: Pass explicit options in startup smoke test**

Set on the `BinlogReaderProcess`:

```cpp
	reader.heartbeat_period_seconds = 1;
	reader.read_timeout_seconds = 5;
```

The existing `ST=` assertion must still pass. This proves the flags are accepted and that an idle stream remains usable while heartbeats arrive.

- [ ] **Step 2: Verify failure shutdown still works**

Keep the existing replication failure test. Add the same valid options to its reader. The listener must still open, the stream failure must still exit nonzero, and the log must still contain `Error in reading binlogs:`.

- [ ] **Step 3: Run focused live tests**

```bash
make -C test/tap
./test/tap/tests/test_basic_startup-t
./test/tap/tests/test_replication_failure_shutdown-t
```

Expected: both pass against the configured MySQL test backend.

- [ ] **Step 4: Commit**

```bash
git add test/tap/tests/test_basic_startup-t.cpp \
  test/tap/tests/test_replication_failure_shutdown-t.cpp
git commit -m "test: cover replication heartbeat and timeout options"
```

---

### Task 5: Verification

- [ ] `make -C test/tap` succeeds
- [ ] `./test/tap/tests/test_replication_timeouts-t` passes
- [ ] `./test/tap/tests/test_mariadb_gtid-t` passes
- [ ] `./test/tap/tests/test_basic_startup-t` passes with explicit flags
- [ ] `./test/tap/tests/test_replication_failure_shutdown-t` passes
- [ ] Existing MySQL TAP matrix remains unchanged
- [ ] Confirm `git diff --check` is clean

Do not claim the silent-partition reproduction is fixed unless an integration test can actually create a network partition and observe `mariadb_rpl_fetch()` failing within the configured timeout.
