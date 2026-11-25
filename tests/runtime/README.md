# Application regression tests

Run `sh tests/runtime/run.sh` from the repository root. A host C compiler is the
only dependency; the temporary executable is removed on exit.

For pixel-accurate host renders of the actual firmware `draw()` function, use:

```sh
sh tests/runtime/run.sh --render /tmp/p4-native-ui
```

This also runs the default tests, then writes binary PPM files for each board's
home, fullscreen, controls, waiting, keyboard, and info pages, plus the vertically longest setup/error info
case. MetalioClaw4 uses 720×720; M5Stack Tab5 uses 720×1280. The keyboard example
uses an explicitly labelled diagnostic background. These images are **host
renders of firmware UI**, not physical device photographs or working Mac video.
The destination's parent directory must already exist.

The test includes the real `firmware/main/main.c` and `native_ui.c` and links the
real `input_core.c`. The small ESP/FreeRTOS stubs allow deterministic scheduling
interleavings and synchronous status callbacks. A recursive take of the same
non-recursive mutex immediately fails the test, instead of hanging.

Covered regressions:

- An idle touch poll and unchanged drop count do not schedule redraws.
- Pairing can call back into the UI synchronously without holding its mutex.
- The mode toolbar action runs outside the UI mutex, rejects invalid profiles,
  and selects distinct small/large stream configurations.
- Wi-Fi reset executes outside the mutex after a continuous two-second hold;
  leaving its button cancels that hold.
- Letterbox pixels and UI overlays do not become video pointer gestures.
- A hidden overlay preserves every pixel of a 720×720 frame; top/bottom edge
  swipes reveal and hide controls without sending extra mouse clicks.
- Central drags, edge taps and horizontal edge drags still reach the HID engine;
  hiding controls releases keyboard modifiers.
- A temporary video pause preserves the last frame and blocks stale-image clicks;
  a new decoded frame resumes fullscreen presentation.
- TCP pressure releases the pipeline lock while waiting; a disconnect cancels a
  waiting old-session frame, and a stalled decoder still has a bounded timeout.
- The compressed-memory budget includes the in-flight decoder AU.
- Overflow and lost/config-changed video require a fresh IDR.
- An old epoch cannot display a frame or erase a newer epoch's queue.
- The pending I420 image is replaced by newer outputs; padded source strides
  are copied into owned planes and the active consumer image stays unchanged.
- Reset clears pending images and prevents obsolete output from reaching UI;
  allocation failure preserves the previously pending image.
- Decoder reset failure prevents a subsequent push and recovers on a later IDR.
- Allocation failure and an individually oversized AU are rejected.
- `SHOWN` counts only new video frames successfully submitted to the board API,
  excluding UI redraws and failed presentations.

These are application logic tests, not FreeRTOS timing, Bluetooth, panel, AirPlay,
or ESP32 performance certification. `tests/decoder` separately runs the actual
OpenH264 decoder against real AVC fixtures; this suite intentionally stubs the
codec to force reset and error conditions.
