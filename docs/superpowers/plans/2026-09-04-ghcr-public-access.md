# GHCR Public Access Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make published GHCR tags anonymously pullable and detect future visibility regressions during release publishing.

**Architecture:** A Bash verifier requests an anonymous pull token and checks the `latest` manifest without downloading layers. The publish job calls it immediately after pushing. The existing Ruby workflow-contract test locks its presence and order; OCI source labels link every runtime image to this repository.

**Tech Stack:** GitHub Actions, Bash, curl, Python 3 JSON parsing, Ruby/Psych, Dockerfiles.

---

## File structure

- Create: `test/verify-ghcr-public-access.sh` — anonymous token and manifest verifier with injectable curl and endpoints.
- Create: `test/verify-ghcr-public-access-test.sh` — fake-curl success and failure tests.
- Modify: `test/verify-release-workflow.rb` — release guard contract.
- Modify: `.github/workflows/ci-test.yml` — guard unit test invocation.
- Modify: `.github/workflows/release.yml` — post-push guard invocation.
- Modify: six current `docker/images/mysqlbinlog-*/Dockerfile` files and `test/verify-runtime-tls-env.sh` — OCI source label and assertion.

### Task 1: Require a release guard before implementation

**Files:**
- Modify: `test/verify-release-workflow.rb`
- Test: `test/verify-release-workflow.rb`

- [ ] **Step 1: Write the failing contract assertion**

Add `PUBLIC_ACCESS_STEP = "Verify public GHCR access"` and `PUBLIC_ACCESS_COMMAND = 'test/verify-ghcr-public-access.sh "${IMAGE_PREFIX}:latest"'`. Add `assert_release_publish_contract(release)`: it requires exactly one Push all tags step, exactly one public-access step, the exact command, and push before guard. Call it before `assert_ci_contract(ci)`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `docker run --rm -v "$PWD:/work:ro" -w /work ruby:3.4-alpine ruby test/verify-release-workflow.rb`

Expected: failure that the public-access step is missing.

- [ ] **Step 3: Commit the failing test**

Run: `git add test/verify-release-workflow.rb && git commit -m "test: require anonymous GHCR release check"`.

### Task 2: Add the testable anonymous-registry verifier

**Files:**
- Create: `test/verify-ghcr-public-access.sh`
- Create: `test/verify-ghcr-public-access-test.sh`
- Test: `test/verify-ghcr-public-access-test.sh`

- [ ] **Step 1: Write the failing shell tests**

Create a temporary executable fake curl, invoke the verifier through `CURL_BIN`, and assert `run_case success 0`, `run_case token_failure 1`, and `run_case manifest_failure 1`. The success response is `{"token":"anonymous-token"}` then `200`; token failure emits an error; manifest failure is `401`. Assert the latter diagnostics mention `anonymous pull token` and `manifest request returned HTTP 401`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `test/verify-ghcr-public-access-test.sh`

Expected: failure because the verifier does not exist.

- [ ] **Step 3: Write minimal implementation**

The verifier begins with `set -euo pipefail`, accepts `<ghcr.io/owner/image:tag>`, and reads `CURL_BIN`, `GHCR_TOKEN_URL`, and `GHCR_REGISTRY_URL` overrides. Reject invalid registry or missing tag. Curl the anonymous token endpoint with `service=ghcr.io` and `scope=repository:<owner/image>:pull`; parse `token` or `access_token` through Python 3 and reject empty output. Curl the manifest with an OCI/Docker Accept header and bearer token, capture only the status code, and reject every code other than `200`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `test/verify-ghcr-public-access-test.sh`

Expected: exit 0.

- [ ] **Step 5: Commit**

Run: `git add test/verify-ghcr-public-access.sh test/verify-ghcr-public-access-test.sh && git commit -m "test: verify anonymous GHCR manifest access"`.

### Task 3: Wire the guard into CI and publishing

**Files:**
- Modify: `.github/workflows/ci-test.yml`
- Modify: `.github/workflows/release.yml`
- Test: `test/verify-release-workflow.rb`, `test/verify-ghcr-public-access-test.sh`

- [ ] **Step 1: Add the invocations**

Immediately after Push all tags, create the step named Verify public GHCR access, running `test/verify-ghcr-public-access.sh "${IMAGE_PREFIX}:latest"`. Immediately after Verify release workflow contract in CI, create the step named Verify GHCR access checker, running `test/verify-ghcr-public-access-test.sh`.

- [ ] **Step 2: Verify the contracts pass**

Run: `docker run --rm -v "$PWD:/work:ro" -w /work ruby:3.4-alpine ruby test/verify-release-workflow.rb && test/verify-ghcr-public-access-test.sh`

Expected: exit 0.

- [ ] **Step 3: Commit**

Run: `git add .github/workflows/ci-test.yml .github/workflows/release.yml && git commit -m "ci: verify published GHCR image is public"`.

### Task 4: Declare OCI source metadata

**Files:**
- Modify: six current runtime Dockerfiles in `docker/images/`
- Modify: `test/verify-runtime-tls-env.sh`
- Test: `test/verify-runtime-tls-env.sh`

- [ ] **Step 1: Write the failing label assertion**

In the Dockerfile loop, reject every file lacking `LABEL org.opencontainers.image.source="https://github.com/sysown/proxysql_mysqlbinlog"`.

- [ ] **Step 2: Run it to verify failure**

Run: `test/verify-runtime-tls-env.sh`

Expected: a missing source-label failure for every current runtime Dockerfile.

- [ ] **Step 3: Write minimal implementation**

Add that exact label directly below the authors label in the six current runtime Dockerfiles.

- [ ] **Step 4: Run it to verify success and commit**

Run: `test/verify-runtime-tls-env.sh && git add docker/images/mysqlbinlog-*/Dockerfile test/verify-runtime-tls-env.sh && git commit -m "build: link GHCR images to source repository"`

Expected: test exits 0, then commit succeeds.

### Task 5: Verify and change the existing package visibility

**Files:**
- Modify: none
- Test: all focused checks

- [ ] **Step 1: Run focused verification**

Run: `docker run --rm -v "$PWD:/work:ro" -w /work ruby:3.4-alpine ruby test/verify-release-workflow.rb && test/verify-ghcr-public-access-test.sh && test/verify-runtime-tls-env.sh && git diff --check master...HEAD`

Expected: exit 0.

- [ ] **Step 2: Make the existing package public**

Open `https://github.com/orgs/sysown/packages/container/package/proxysql-mysqlbinlog/settings`, choose **Change visibility**, select **Public**, type `proxysql-mysqlbinlog`, and confirm. The change is irreversible.

- [ ] **Step 3: Confirm anonymous end-to-end access**

Run: `test/verify-ghcr-public-access.sh ghcr.io/sysown/proxysql-mysqlbinlog:latest && docker pull ghcr.io/sysown/proxysql-mysqlbinlog:latest`

Expected: the checker gets HTTP 200 and Docker pulls without login.
