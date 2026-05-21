# proxysql_binlog_reader test suite

Integration tests for `proxysql_binlog_reader`. TAP-style C++ tests run
against a docker-based MySQL fleet.

## Layout

```
test/
├── tap/                    # framework + test sources
│   ├── tap.{h,cpp}         # TAP primitives (plan/ok/diag/exit_status)
│   ├── command_line.*      # env-driven config loader
│   ├── binlog_reader_*.*   # reader process wrapper + on-wire client
│   ├── mysql_client.*      # thin libmysqlclient wrapper
│   ├── tap_utils.h         # setup_reader() helper
│   ├── run.sh              # per-version test runner
│   ├── Makefile            # builds libtap.a + tests/*-t
│   └── tests/              # *-t.cpp TAP binaries
├── infra/                  # docker-based infra + entry-point scripts
│   ├── Dockerfile.infra    # mysql service (dbdeployer + 5 versions)
│   ├── Dockerfile.runner   # runner service (runtime libs only)
│   ├── docker-compose.yml  # mysql + runner services
│   ├── entrypoint.sh       # dbdeployer deploy logic
│   ├── start-test.sh       # bring up infra + run tests
│   └── clean-infra.sh      # tear down infra (+ optional --clean-logs)
├── logs/                   # per-run test logs (gitignored)
│   └── <RUN_ID>/           # timestamped (UTC, e.g. 20260521T163022Z)
│       ├── runner.log
│       └── mysql-<V>/
│           ├── reader.log
│           └── <test>.log
└── old/                    # legacy smoke tests (pre-TAP framework)
```

## Prerequisites

Two artifacts must be built **on the host** before running tests; the
runner container links against them via bind mount.

```sh
make build-ubuntu24          # builds proxysql_binlog_reader at repo root
make -C test/tap             # builds test/tap/libtap.a + tests/*-t
```

## Running tests

### Containerized (recommended)

Needs only docker on the host.

```sh
test/infra/start-test.sh
```

Does:

1. Generate `RUN_ID` (UTC timestamp).
2. Create `test/logs/${RUN_ID}/`.
3. Sanity-check host artifacts exist.
4. `docker compose up -d mysql` (idempotent).
5. `docker compose run --rm runner` — streams output to terminal AND
   writes to `test/logs/${RUN_ID}/runner.log`.

Exits with the TAP suite's exit code. Infra is left running for
post-mortem.

### Host mode (fast iteration)

Needs `g++`, `libmysqlclient-dev`, and `mysql_config` on the host.

```sh
docker compose -f test/infra/docker-compose.yml up -d mysql
MYSQL_VERSIONS=84 test/tap/run.sh
```

`run.sh` polls the `binlog-reader-infra` container's healthcheck via
`docker inspect` before launching tests. Set `INFRA_CONTAINER=""` to
disable when targeting non-local MySQL.

## Teardown

```sh
test/infra/clean-infra.sh                 # docker compose down -v
test/infra/clean-infra.sh --clean-logs    # also wipe test/logs/*Z dirs
```

`--clean-logs` uses a throwaway alpine container because logs may
contain root-owned files (the runner runs as root by default).

## MySQL fleet

The `mysql` container hosts five versions side-by-side via
[dbdeployer]. One sandbox per token in `MYSQL_VERSIONS`
(default: `57 80 84 90 94`), each on a fixed port.

| Token | MySQL line | Host port |
|-------|------------|----------:|
| `57`  | 5.7        |      3357 |
| `80`  | 8.0        |      3380 |
| `84`  | 8.4        |      3384 |
| `90`  | 9.0        |      3390 |
| `94`  | 9.4        |      3394 |

All sandboxes share `root` / `root` credentials with `root@'%'`
granted. Reachable from the host (`127.0.0.1:3384`, …) and from inside
the runner via the compose network (`mysql:3384`, …).

Each sandbox is configured for binlog + GTID:

- `log-bin=mysql-bin`, unique `server-id`
- `gtid-mode=ON`, `enforce-gtid-consistency=ON`
- `binlog-format=ROW`
- `log-slave-updates=ON` (5.7/8.0) / `log-replica-updates=ON` (8.4+)
- 8.4 enables `mysql-native-password=ON` for replication-client
  compatibility; 9.x uses `caching_sha2_password` only.

To poke a sandbox directly:

```sh
mysql -h 127.0.0.1 -P 3384 -u root -proot
```

[dbdeployer]: https://github.com/ProxySQL/dbdeployer

## Log layout

```
test/logs/<RUN_ID>/
├── runner.log                  # full TAP stream (across versions)
└── mysql-<V>/                  # one dir per MySQL version
    ├── reader.log              # reader stdout/stderr, appended across tests
    └── <test_name>.log         # one per *-t binary, just that test's TAP output
```

The reader writes to its log file via `dup2` of stdout/stderr in the
child process (the reader's `-L` flag is a no-op in foreground mode).
This is set up by `BinlogReaderProcess::start()`, with the path coming
from `run.sh` via `BINLOG_READER_LOG_FILE`.

## Environment variables

Recognized by `test/tap/run.sh` and the test binaries:

| Var | Default | Effect |
|---|---|---|
| `MYSQL_VERSIONS` | `57 80 84 90 94` | Versions to iterate |
| `MYSQL_HOST` | `127.0.0.1` | Override host (containerized run sets `mysql`) |
| `MYSQL_USER` | `root` | |
| `MYSQL_PASSWORD` | `root` | |
| `BINLOG_READER_BIN` | `<repo>/proxysql_binlog_reader` | Empty → connect mode |
| `BINLOG_READER_HOST` | `127.0.0.1` | Reader endpoint in connect mode |
| `BINLOG_READER_PORT` | `6020` | |
| `BINLOG_READER_LOG_DIR` | unset | When set, run.sh writes per-version logs under this dir |
| `INFRA_CONTAINER` | `binlog-reader-infra` | Empty → skip healthcheck wait |
| `PROXY_ADMIN_HOST` | `127.0.0.1` | Reserved for future proxysql-compat test |
| `PROXY_ADMIN_PORT` | `6032` | |

## Notes

- The fleet lives in a single container by design — one canonical
  infra, iterated by the runner.
- The runner bind-mounts the whole repo (not individual binaries), so
  test-source edits on the host are visible on the next run without an
  image rebuild.
- `entrypoint.sh` pre-deletes any stale dbdeployer sandbox before
  redeploying, so the `mysql` container is safe to stop/start
  repeatedly.
- Override individual MySQL tarball URLs via build args when upstream
  moves them, e.g.:
  ```sh
  docker compose -f test/infra/docker-compose.yml build mysql \
      --build-arg MYSQL_TARBALL_94_URL=https://.../mysql-9.4.1-...tar.xz
  ```
