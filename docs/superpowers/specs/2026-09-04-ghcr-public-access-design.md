# GHCR public access design

## Goal

Resolve issue #42: anonymous users must be able to pull the published
`ghcr.io/sysown/proxysql-mysqlbinlog` release tags. Ensure a release fails if
the registry does not expose the pushed `latest` manifest anonymously.

## Root cause

The container package was created with private visibility. The public source
repository and successful release workflow do not make an existing GHCR
container package public. An anonymous pull-token request for the package
returns `UNAUTHORIZED`.

## Immediate remediation

An administrator will open the package settings for
`sysown/proxysql-mysqlbinlog` and change its visibility to **Public**. GitHub
documents this as irreversible. This is an external package setting, not a
repository-file change.

## Release guard

The `publish` job will retain its existing push step and add a post-push
validation step. It will:

1. Request an anonymous GHCR pull token for
   `repository:sysown/proxysql-mysqlbinlog:pull`.
2. Fail with a clear diagnostic if the registry does not issue a token.
3. Use that token to request the `latest` OCI/Docker manifest.
4. Fail unless the request returns HTTP 200.

The guard exercises the exact unauthenticated path used by `docker pull`, does
not expose credentials, and avoids downloading image layers.

## Image metadata

Each release image Dockerfile will declare
`org.opencontainers.image.source=https://github.com/sysown/proxysql_mysqlbinlog`.
This links newly created GHCR images to the source repository and makes that
relationship explicit. It does not substitute for making the existing package
public.

## Verification

* Unit-style shell validation will exercise the release guard's success and
  failure handling with local HTTP fixtures or a testable helper script.
* The release workflow syntax will be validated locally where available.
* After the package visibility change, an unauthenticated token and manifest
  request for `latest` must both succeed; `docker pull` is the final
  end-to-end confirmation.
