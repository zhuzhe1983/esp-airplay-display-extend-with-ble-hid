# P4 Native Display

**Inspired by [HoppScreen](https://github.com/Rakesh717/HoppScreen). Built for native macOS AirPlay.**

面向 **M5Stack Tab5** 和 **MetalioClaw4** 的 ESP32-P4 触摸副屏固件。Mac 从系统自带的“屏幕镜像”连接设备，通过 AirPlay 传输桌面画面；设备通过独立的 BLE HID 回传键盘和触控板式指针输入。Mac 端无需安装专用发送程序。

## 功能

- **macOS 原生接入**：从“控制中心 → 屏幕镜像”连接，在系统提供的选项中选择扩展或镜像显示。
- **最新画面优先**：解码和显示转换分开运行，新画面覆盖待显示的旧画面，避免补播积压内容。
- **两档显示模式**：`FASTSYNC` 优先响应，`SHARP` 请求更高分辨率，设备保存所选档位。
- **蓝牙键鼠**：屏幕键盘、修饰键与组合键，以及相对指针、轻点、长按拖动和双指滚动。
- **全屏与滑动控制条**：首帧到达后自动全屏，从屏幕上下边缘滑动呼出或收起控制条。
- **设备端配网**：设置热点、网页或 USB 配网，网络配置保存在 NVS。

## 设备与 macOS 兼容性

| 设备 | 板级适配 | 验证状态 |
| --- | --- | --- |
| **M5Stack Tab5** | 官方 BSP 1.3.3，三代面板与触控识别，ESP32-C6 Wi-Fi + BLE | 已提供板级适配和独立构建目标；实物投屏与键鼠验收待完成 |
| **MetalioClaw4** | NV3051F / FL7707N，GT911、TCA9555，ESP32-C5 Wi-Fi + BLE | NV3051F 实物已完成 Mac AirPlay 扩展显示、视频解码和 BLE 键鼠连接验证；持续性能仍在优化 |

当前 Mac 实机记录来自 **Apple M5 Max、macOS 27.2.1 与 MetalioClaw4**，详见[本地安装与验证记录](docs/LOCAL_SETUP.md)。其他 Mac、macOS 版本和 Tab5 实物仍需分别验证。

目前的兼容范围：

- 接入 macOS 原生 AirPlay 屏幕镜像菜单，支持已验证会话中的扩展桌面；本项目不实现 Apple Sidecar。
- Mac 决定最终的桌面和编码尺寸。本机 Claw4 仍使用 **800×600 逻辑桌面、720×540 视频**，尚未实现所请求的 720×720 原生方形桌面。
- BLE 输入采用标准键盘和相对鼠标。光标可以跨 Mac 显示器移动，尚未实现自动绑定副屏的绝对触摸定位。
- Apple Pencil、Sidecar 边栏 / Touch Bar、AWDL 和设备端声音播放尚未实现。

## 连接 Mac

1. 烧录与板型、芯片 revision 和面板匹配的固件。无线协处理器需运行兼容 ESP-Hosted 的 **Wi-Fi + BLE** 固件，见[协处理器说明](firmware/coprocessor/README.md)。
2. 首次启动时连接屏幕显示的 `P4-Display-xxxx` 热点，输入屏幕上的随机密码，打开 `http://192.168.4.1`，将设备配置到与 Mac 相同的局域网。也可使用 [USB 配网](docs/USB_PROVISIONING.md)。
3. 设备显示 IP 后，在 Mac“控制中心 → 屏幕镜像”选择带 `FastSync` 或 `Sharp` 后缀的设备名；连接后可以正常关闭菜单。在 macOS 显示器设置中选择扩展或镜像方式。
4. 在 Mac“系统设置 → 蓝牙”另外配对 **M5Stack Tab5 Input** 或 **MetalioClaw4 Input**。启动时有三分钟配对窗口；需要重新配对时，点设备的 `PAIR`。
5. 点 `KEYBOARD` 打开软键盘。修饰键支持 Cmd / Option / Ctrl / Shift；`IME` 发送 Ctrl+Space，按 Mac 当前输入源设置切换输入法。中文文字由 Mac 输入法处理。

投屏与蓝牙输入是两条独立连接。视频能显示，并不代表蓝牙报告已经就绪；设备顶部的 `INPUT READY` 表示输入连接就绪。

## 屏幕操作

首帧成功显示后自动隐藏标题和工具条，全屏显示画面；发送端尺寸与面板不一致时等比缩放并保留黑边。

- **呼出控制条**：从上边缘向下滑，或从下边缘向上滑。
- **收起控制条**：从标题向上滑，或从控制条上方中央横条向下滑。
- **触控板操作**：轻点左键、长按后移动拖动、双指移动滚动、双指轻点右键。
- **软键盘**：`KEYBOARD` 打开，`HIDE KEYS` 收起；隐藏控制条时释放按键和修饰键。
- **状态与配网**：`INFO` 查看连接状态；在该页按住 `WIFI RESET` 两秒清除网络设置并重启。

短暂等待视频时保留最后一帧，显示 `WAITING FOR VIDEO - LAST FRAME`；新画面到达后恢复全屏。等待期间暂停画面触控，控制条仍可使用。

## 显示模式

点控制条第四个按钮切换档位。切换会重新发布 AirPlay 服务，随后在 Mac 连接相应名称；Wi-Fi 配置和蓝牙绑定保留。默认使用 `FASTSYNC`。

| 档位 | M5Stack Tab5 请求参数 | MetalioClaw4 请求参数 | 策略 |
| --- | --- | --- | --- |
| **FASTSYNC** | 320×180，最高 10 fps | 360×360，最高 30 fps | 请求较低编码尺寸，优先响应 |
| **SHARP** | 640×360，最高 10 fps | 720×720，最高 30 fps | 请求更高编码尺寸，优先细节 |

设备名称使用 `<设备名> FastSync` / `<设备名> Sharp`。表中是当前配置的**请求参数**，不是实际桌面尺寸或实测帧率；macOS 可能选择其他参数。Tab5 默认参数尚待 Mac 实物会话验证。

两档共用“最新画面优先”管线：AVC 解码按参考帧依赖顺序处理，解码后的 I420 图片复制到独立缓冲；待转换区只保留最新一帧，新帧直接覆盖旧帧，不回头补播。正在转换的图片保持独占，避免被改写。

## 性能目标与实测

对交互式触摸副屏，优先优化从操作到屏幕变化的延迟。下面是**后续调优的工程目标**，不是 AirPlay 标准，也不是当前固件已达到的指标；部分目标需要调整当前默认请求参数。

| 指标 | 建议目标 |
| --- | --- |
| 单连接有效吞吐 | 稳定达到 10 Mbps，争取 20 Mbps 以上；最终按实际视频峰值码率预留余量 |
| 负载下网络往返 RTT | 通常不超过 20 ms，95% 的采样不超过 50 ms |
| 操作到物理画面的端到端延迟 | 先控制在 150 ms 内，再争取 100 ms 内 |
| 有效显示帧率 | 先稳定 10–15 fps，再争取 20–30 fps；允许丢弃过时输出 |
| 颜色转换耗时 | 争取 10–15 ms / 帧 |
| 待显示队列 | 只保留最新一帧，不持续积压历史画面 |

2026-11-25 的 Claw4 业务路径测速约 **2–3.5 Mbps**，一轮传输时 TCP 平均 RTT 约 **132 ms**，并出现重传。此前的软件 RGB 转换约 **110 ms / 帧**。这些数据表明网络与图像处理都需要优化，不能将系统显示的 Wi-Fi 链路速率当作有效吞吐。

上述吞吐测试包含设备控制解析器开销，是链路能力的下界；测试时没有 AirPlay 视频流。`frame_age_ms` 仅计解码快照到 UI 的年龄，不能代替端到端延迟。最新显示管线的实机会话结果继续记录在[安装与测速记录](docs/LOCAL_SETUP.md)，解码实现见 [VIDEO.md](docs/VIDEO.md)。

## 构建与烧录

使用 **ESP-IDF v5.5.4**，安装 `esp32p4` 工具链并执行 SDK 的 `export.sh`。两块板使用独立的构建目录和 sdkconfig。

```sh
python tools/build.py m5stack_tab5 build
python tools/build.py metalio_claw4 build
```

修改板级或请求参数：

```sh
python tools/build.py m5stack_tab5 menuconfig
python tools/build.py metalio_claw4 menuconfig
```

使用工程标准分区布局时，以 Tab5 为例烧录和观察日志；将端口换成实际设备端口：

```sh
python tools/build.py m5stack_tab5 flash --port /dev/cu.usbmodemXXXX
python tools/build.py m5stack_tab5 monitor --port /dev/cu.usbmodemXXXX
```

保留原厂 OTA 分区布局的 Claw4 应按[本地安装记录](docs/LOCAL_SETUP.md)更新实际应用分区，避免默认整套烧录覆盖原布局。芯片 revision、面板变体和引脚见 [BOARDS.md](docs/BOARDS.md)；协处理器固件需单独核对，本工程不会自动改写 C6 / C5。

## 测试与诊断

运行主机侧真实解码、协议和输入回归测试：

```sh
python tools/test_host.py
```

需要 C/C++ 编译器、CMake、Python 和 Bash。测试视频已包含在源码中，只有重新生成 fixture 才需要 FFmpeg / libx264。主机测试速度不代表 P4 性能。

测速时将地址和名称替换为设备屏幕显示的实际值：

```sh
python3 tools/measure_link.py --host 192.168.1.100 \
  --expect-name 'M5Stack Tab5 FastSync' --seconds 5 \
  --output .build/link-check.json
```

测速脚本先核对接收端身份，再发送有界的 RTSP 请求体，完整收到确认才计入有效载荷；不发起配对或视频会话。测试会短暂占用局域网带宽。

- [原生 AirPlay 协议](docs/AIRPLAY_PROTOCOL.md)
- [视频解码与性能边界](docs/VIDEO.md)
- [蓝牙输入与 Mac 配对](docs/INPUT.md)
- [现场验收步骤](docs/ACCEPTANCE.md)
- [初始构建与测试记录](docs/VALIDATION.md)
- [Mac 实物安装与测速记录](docs/LOCAL_SETUP.md)

## 致谢与许可证

设计灵感来自 [HoppScreen](https://github.com/Rakesh717/HoppScreen)。本项目采用原生 AirPlay 接收与独立 BLE HID 的实现路线。

AirPlay 接收实现 **based on [ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent) by Henry Yuan**，保留原始 MIT copyright、LICENSE、NOTICE 和 AirPlay 子目录的 GPL 声明。公开演示、视频或文章请同时注明该上游项目。

同时感谢 [UxPlay](https://github.com/FDH2/UxPlay)、[OpenH264 ESP32 port](https://github.com/jonasborn/openh264-esp32)、Cisco OpenH264、[M5Stack Tab5 BSP](https://components.espressif.com/components/espressif/m5stack_tab5_noglib/versions/1.3.3)、[CloudZao/MetalioClaw4](https://github.com/CloudZao/MetalioClaw4) 与 Espressif。固定版本、来源和许可证见 [THIRD_PARTY.md](THIRD_PARTY.md)。

本工程新代码采用 GPL-3.0-or-later；各上游文件保留原许可证。分发链接后的固件时，应同时提供对应完整源码和构建信息。
