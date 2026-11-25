#!/bin/sh
set -eu
runtime_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
runtime_binary=$(mktemp /tmp/p4-runtime-test.XXXXXX)
trap 'rm -f "$runtime_binary"' EXIT HUP INT TERM
cc -std=gnu17 -Wall -Wextra -Werror \
  -I"$runtime_root/tests/runtime/stubs" \
  -I"$runtime_root/firmware/main" \
  -I"$runtime_root/firmware/components/video_decoder/include" \
  -I"$runtime_root/firmware/components/airplay_receiver/include" \
  -I"$runtime_root/firmware/components/display_board/include" \
  -I"$runtime_root/firmware/components/input_bridge/include" \
  "$runtime_root/tests/runtime/runtime_test.c" \
  "$runtime_root/firmware/components/input_bridge/input_core.c" \
  "$runtime_root/firmware/main/display_profile.c" \
  -o "$runtime_binary"
"$runtime_binary" "$@"
