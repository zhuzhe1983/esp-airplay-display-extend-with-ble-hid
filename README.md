# P4 Native Display

面向 **MetalioClaw4** 与 **M5Stack Tab5** 的 ESP-IDF 多设备实验工程。目标是从 Mac 自带的“屏幕镜像”入口连接 P4 触摸屏，以独立的标准蓝牙 HID 回传键盘与指针输入。Mac 不需要安装 HoppScreen 或本项目的发送程序。

**交付性质：开发源码版，尚未完成 Mac + 实物设备端到端验收。** 已写入真实 AirPlay 接收、H.264 解码、面板驱动及 HID 实现，软件验证见 [验证记录](docs/VALIDATION.md)。这不是经过认证的 AirPlay 产品，也没有实现 Apple Sidecar 的 iPad 身份、账户配对、原生边栏和触控栏。

## 为什么不继续直接移植 HoppScreen

用户最终要求的是“只用 macOS 系统投屏”。HoppScreen 自己的 Mac 发送端与浏览器客户端不符合这个约束。因此本工程沿用两块指定硬件，采用原生 AirPlay 接收路线重新组织固件。没有把 HoppScreen Mac App、WebCodecs 浏览器或中转电脑放进最终链路。

截图中的 iPad 扩展屏、边栏、Touch Bar 是 Sidecar 体验。普通 AirPlay 与 Sidecar 并不是同一个输入协议；将 Bonjour 广播名称改成 iPad 不能获得相同能力。完整非 Apple Sidecar 接收端仍缺少本项目可复用、可验证的实现。

## 当前实现

| 模块 | 已写入代码 | 实际验证边界 |
| --- | --- | --- |
| 原生 AirPlay | Bonjour、legacy 配对、FairPlay、RTSP、NTP、H.264 收流与解密 | 协议解析测试通过；真实 Mac 会话待测 |
| H.264 | OpenH264 scalar 软件解码，Baseline/Main/High、CABAC、B 帧，RGB565 缩放与黑边 | 真实测试流与 FFmpeg 参考比较通过；P4 速度待测 |
| MetalioClaw4 | NV3051F/FL7707N、GT911、TCA9555、电源及 C5 SDIO 配置 | 编译状态见验证记录；屏幕电气/时序待上板 |
| Tab5 | 官方 BSP 1.3.3，三代面板/触控识别、C6 电源/天线/SDIO | 编译状态见验证记录；实物各版本待测 |
| 触控 | 相对指针、单击、长按拖动、双指滚动与双指轻点右键 | 手势与 HID 报文测试；Mac 指针体验待测 |
| 键盘 | 屏幕键盘、Cmd/Opt/Ctrl/Shift、方向键、复制粘贴、输入法快捷键 | HID 报文测试；BLE 配对与 Mac IME 待测 |
| 配网 | 首次启动 WPA2 设置热点、浏览器表单、NVS 保存、设备端重置 | 编译检查；无线和设置页面上板待测 |

**尚未完成的关键能力：** 真正 Sidecar 会话、扩展屏的确定性绝对触摸定位、Apple Pencil 压感/倾斜、AWDL 点对点连接、声音播放。实验性绝对鼠标报告默认不开放，必须由开发者显式开启并验证显示器映射。基础触控使用触控板式相对鼠标。

## 构建

使用 ESP-IDF **v5.5.4**，安装 `esp32p4` 工具链并执行该 SDK 的 `export.sh`。工程保留了组件锁文件；AirPlay 与软件解码器的必要源码已随包提供。

```sh
python tools/build.py metalio_claw4 build
python tools/build.py m5stack_tab5 build
```

配置屏幕型号、请求画面尺寸等：

```sh
python tools/build.py metalio_claw4 menuconfig
```

烧录指定 P4 设备并观察串口：

```sh
python tools/build.py metalio_claw4 flash --port /dev/ttyACM0
python tools/build.py metalio_claw4 monitor --port /dev/ttyACM0
```

macOS 上将端口替换为实际 `/dev/cu.usbmodem...`。这个工具只是构建/烧录脚本，不是投屏时需要运行的 Mac 客户端。固件替换原厂 P4 应用；恢复原厂系统请使用厂商对应固件。两份构建使用独立 sdkconfig 和 build 目录。P4 芯片 revision 与屏幕变体说明见 [BOARDS.md](docs/BOARDS.md)。

## 第一次使用

1. 确认无线协处理器运行兼容 ESP-Hosted 的 **Wi-Fi + BLE** 固件。P4 固件不会自动改写 C5/C6；只支持 Wi-Fi 的从机可能能投屏但不能启用键盘。具体证据与路径见 [协处理器说明](firmware/coprocessor/README.md)。
2. 首次启动时连接屏幕显示的 `P4-Display-xxxx` 热点，输入屏幕上的随机密码。在浏览器打开 `http://192.168.4.1`，填写与 Mac 相同局域网的 Wi-Fi。Tab5 使用 2.4 GHz；Claw4 的 C5 可以使用适配的双频网络。
3. 设备重新启动并显示 IP 后，在 Mac 控制中心的“屏幕镜像”中选择设备。能否完成该 Mac 版本的握手、发出画面及提供扩展选项，以真实会话为准。
4. 在 Mac“系统设置 → 蓝牙”中另外配对 `MetalioClaw4 Input` 或 `M5Stack Tab5 Input`。设备启动时开启三分钟配对窗口；窗口结束后点设备的 `PAIR` 重开。投屏与 HID 是两条独立连接。
5. 点 `KEYBOARD` 打开软键盘；点修饰键使其选中，再点普通键发送组合键。`IME` 发出 Ctrl+Space，遵循 Mac 当前输入源快捷键设置。中文由 Mac 输入法将拼音等键击转换，不直接发送 Unicode 字符。

若电脑无法连接设置热点，可使用[USB 配网](docs/USB_PROVISIONING.md)，直接保存设备网络配置。

触控区：轻点为左键，长按后移动为拖动，双指移动为滚动，双指轻点为右键。软键盘会覆盖画面的一部分，使用 `HIDE KEYS` 收起。设备 UI 使用英文短标签。`INFO` 查看连接状态；在该页按住 `WIFI RESET` 两秒清除 Wi-Fi 设置并重新启动。

## 画面与性能

默认向发送端请求 **640×360、最高 10 fps**。这些是协商期望，不是实测性能，也不能保证 macOS 完全遵守。P4 具备 H.264 编码硬件，并没有可供本工程使用的 H.264 硬件解码器；高分辨率桌面的软件解码可能超出可用资源。

解码输入默认最多 1280×1280、8-bit 4:2:0 progressive、最多四个参考帧、单 AU 1 MiB。压缩队列上限 4 MiB；超限后清空并等待 IDR，避免继续解码已经丢失参考链的 P/B 帧。若发送端不再发关键帧，需要停止并重新开始系统镜像。不能通过丢任意 P 帧获得“稳定高帧率”。

`STREAMING` 表示已收到有效压缩 AU；必须同时看到解码帧数增加及物理屏幕的动态图像，才算真实显示。上游 ESP32 接收器存在 [Mac 握手完成但无视频的已知记录](https://github.com/onehumanbeing/esp32-iphone-agent/issues/46)。本版对齐了 UxPlay 的时钟启动时序与会话密钥条件，但还没有实机证据证明解决该问题。

## 开发与测试

```sh
python tools/test_host.py
```

需要主机 C/C++ 编译器、CMake、Python 与 Bash。测试 fixture 已包含在源码中；只有重新生成 fixture 才需要 FFmpeg/libx264。主机解码速度不能用来推断 P4 帧率。

- [协议实现与诊断](docs/AIRPLAY_PROTOCOL.md)
- [视频边界及测试](docs/VIDEO.md)
- [板级驱动与引脚](docs/BOARDS.md)
- [输入及 Mac 配对](docs/INPUT.md)
- [现场验收步骤](docs/ACCEPTANCE.md)
- [版本验证记录](docs/VALIDATION.md)

## 上游与许可证

Based on [ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent) by **Henry Yuan**. 保留原始 MIT copyright、LICENSE、NOTICE 与 AirPlay 子目录的 GPL 声明。公开演示、视频或文章请同时注明这个上游项目。

同时感谢 [UxPlay](https://github.com/FDH2/UxPlay)、[OpenH264 ESP32 port](https://github.com/jonasborn/openh264-esp32)、Cisco OpenH264、[CloudZao/MetalioClaw4](https://github.com/CloudZao/MetalioClaw4)、M5Stack 与 Espressif 的开源实现。来源及固定版本见 [THIRD_PARTY.md](THIRD_PARTY.md)。本工程新代码采用 GPL-3.0-or-later；各上游文件保留原许可证。分发链接后的固件时应同时提供对应完整源码与构建信息。
