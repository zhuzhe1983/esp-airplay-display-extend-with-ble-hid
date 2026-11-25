# Third-party provenance

| Project | Pinned revision / version | Used material | License |
| --- | --- | --- | --- |
| [ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent) by Henry Yuan | `90f450ea7c2741901a2b4102c8d209c930961912` | AirPlay receiver and binary plist parser, adapted to native ESP-IDF | MIT original parts; GPL AirPlay/PlayFair parts |
| [UxPlay](https://github.com/FDH2/UxPlay) | `48f5ce47c9ef694afccfac907c9e8828329657fe` | Protocol comparison; inherited upstream receiver lineage | GPL-3.0 |
| [OpenH264 ESP32](https://github.com/jonasborn/openh264-esp32) | `4a7856e61919d2c7b580413151f3cfb1323a3585` | Scalar decoder/common/API, PSRAM port, local bounds/OOM fixes | BSD-2-Clause; upstream NOTICE retained |
| [MetalioClaw4](https://github.com/CloudZao/MetalioClaw4) | `ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950` | LCD init tables, hardware configuration | MIT |
| [ESP-IDF](https://github.com/espressif/esp-idf) | v5.5.4 / `735507283d5b2f9fb363a1901172dbd9e847945d` | SDK, NimBLE integration APIs | Apache-2.0 and component licenses |
| [ESP-Hosted MCU](https://github.com/espressif/esp-hosted-mcu) | 2.12.12 / `471900d45c3b9208da10a01beb200b97f15a161a` | SDIO, Wi-Fi remote, BLE HCI | Apache-2.0 |
| [M5Stack Tab5 BSP](https://components.espressif.com/components/espressif/m5stack_tab5_noglib/versions/1.3.3) | 1.3.3 | Official display/touch/power BSP | Apache-2.0 |

Managed components are resolved from the Espressif Component Registry. `firmware/dependencies.lock` records exact downloaded versions and hashes. These sources are not duplicated in the repository ZIP; the build downloads them through the official manager. Vendored protocol and decoder source includes its original license texts in each component. Test H.264 fixtures were generated locally from synthetic `testsrc2` with FFmpeg/libx264 and contain no captured user screen content.

New project code is GPL-3.0-or-later. The combined firmware contains GPL receiver code. Preserve corresponding source and build instructions when distributing it. No Apple private certificate, account token, vendor identity credential, or user Wi-Fi password is included. The legacy AppleTV model string is a protocol compatibility field inherited from the receiver; the product does not claim to be an iPad or an Apple-certified receiver.
