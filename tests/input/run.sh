#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
component_dir="$project_dir/firmware/components/input_bridge"
test_binary="$(mktemp "${TMPDIR:-/tmp}/p4-input-test.XXXXXX")"
trap 'rm -f "$test_binary"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$component_dir/include" -I"$component_dir" \
  "$project_dir/tests/input/test_input.c" "$component_dir/input_core.c" "$component_dir/input_tx_queue.c" \
  -o "$test_binary"
"$test_binary"
