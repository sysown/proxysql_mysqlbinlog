#!/usr/bin/env bash
# Verify the bundled libdaemon bootstrap can configure on native ARM64.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
patch_file="$repo_root/patches/libdaemon-aarch64-config.patch"
work_dir=$(mktemp -d)
cleanup() {
  status=$?
  rm -rf "$work_dir"
  exit "$status"
}
trap cleanup EXIT

test -f "$patch_file"
grep -Fq 'patch -p1 < patches/libdaemon-aarch64-config.patch' "$repo_root/Makefile"

tar -xzf "$repo_root/libdaemon-0.14.tar.gz" -C "$work_dir"
ln -s libdaemon-0.14 "$work_dir/libdaemon"
patch -d "$work_dir" -p1 < "$patch_file"

fake_bin="$work_dir/fake-bin"
mkdir "$fake_bin"
cat > "$fake_bin/uname" <<'EOF'
#!/bin/sh
case "$1" in
  -m) echo aarch64 ;;
  -s) echo Linux ;;
  -r) echo 6.0 ;;
  -v) echo test ;;
  *) exec /usr/bin/uname "$@" ;;
esac
EOF
chmod +x "$fake_bin/uname"

test "$(PATH="$fake_bin:$PATH" "$work_dir/libdaemon/config.guess")" = \
  'aarch64-unknown-linux-gnu'
test "$("$work_dir/libdaemon/config.sub" aarch64-unknown-linux-gnu)" = \
  'aarch64-unknown-linux-gnu'
