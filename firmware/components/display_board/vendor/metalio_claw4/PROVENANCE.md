# MetalioClaw4 panel driver provenance

The four `esp_lcd_nv3051f.*` / `esp_lcd_fl7707n.*` files were copied from
[CloudZao/MetalioClaw4](https://github.com/CloudZao/MetalioClaw4/tree/ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950/main/boards/metalio-claw-4),
commit `ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950`.
The repository's MIT license is included in this directory. Keep this notice
and the license with redistributed copies. Panel command tables are retained.

Board wiring and timings in `src/board_claw4.c` are based on the same commit's
`config.h`, `metalio-claw-4.cc`, `main/boards/common/IOExpander.hpp`, and `sdkconfig`.
Our presenter uses two framebuffers rather than the upstream LVGL triple-buffer
path. Camera, audio, charging-register customization, SD card and cellular
features are not initialized by this component.
