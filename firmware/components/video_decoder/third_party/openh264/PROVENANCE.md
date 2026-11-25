# OpenH264 decoder source provenance

Vendored from https://github.com/jonasborn/openh264-esp32 at commit
`4a7856e61919d2c7b580413151f3cfb1323a3585`.

Included: `.h`/`.cpp` files from `codec/api/wels`, `codec/common/inc`,
`codec/common/src`, `codec/decoder/core/inc`, `codec/decoder/core/src`,
`codec/decoder/plus/inc`, and `codec/decoder/plus/src`, plus original `LICENSE`
and `NOTICE`. Encoder sources and architecture assembly are not included.

Local changes: `codec/common/src/memory_align.cpp` rejects alignment/size
overflow and, on ESP, requires PSRAM instead of silently falling back to the
internal heap. `codec/decoder/plus/src/welsDecoderExt.cpp` uses checked
`std::nothrow` allocations for decoder/control objects and handles a missing
thread-context allocation during initialization/cleanup. Source and notices are included directly; no bootstrap download
is required to build. See the original NOTICE for codec patent/licensing terms.

This is a software H.264 decoder. ESP32-P4's H.264 encoder does not accelerate it.
