#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
out="${TMPDIR:-/tmp}/test_amy_unix_socket"

cc \
  -std=c11 \
  -O2 \
  -Wall \
  -Wextra \
  -Werror \
  -pthread \
  -I"$repo_root/src" \
  "$repo_root/src/amy_unix_socket.c" \
  "$repo_root/tests/test_amy_unix_socket.c" \
  -o "$out"

"$out"
rm -f "$out"
