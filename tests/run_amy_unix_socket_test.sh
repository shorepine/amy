#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp "${TMPDIR:-/tmp}/amy-unix-socket-test.XXXXXX")"
trap 'rm -f "$out"' EXIT

cc \
  -std=c11 \
  -O1 \
  -g \
  -Wall \
  -Wextra \
  -Werror \
  -pthread \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  -I"$repo_root/src" \
  "$repo_root/src/amy_unix_socket.c" \
  "$repo_root/tests/test_amy_unix_socket.c" \
  -o "$out"

ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  "$out"
