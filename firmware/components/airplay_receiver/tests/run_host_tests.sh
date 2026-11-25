#!/usr/bin/env bash
set -euo pipefail
component_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp "${TMPDIR:-/tmp}/p4-airplay-bounds.XXXXXX")"
trap 'rm -f "$test_binary"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$component_dir/core" -I"$component_dir/bplist" \
  "$component_dir/tests/test_packet_bounds.c" \
  "$component_dir/core/h264_packet.c" "$component_dir/bplist/bplist.c" \
  -o "$test_binary"
"$test_binary"
