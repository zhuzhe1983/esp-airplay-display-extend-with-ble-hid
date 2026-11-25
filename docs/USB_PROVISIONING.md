# USB 配网

当电脑无法连接设置热点时，可通过 P4 的 USB Serial/JTAG 接口写入 Wi-Fi 配置。
本功能与网页配网共用 `p4_network` NVS 命名空间，保存后设备自动重启。密码不编译进固件，也不会回显到串口日志。

在固件完成启动后运行，需要 Python 与 pyserial：

```sh
python tools/configure_wifi.py --port /dev/cu.usbmodem1101 --ssid "your-network" --name "MetalioClaw4 Display"
```

脚本会单独提示输入密码，不将密码作为命令行参数；先等待 `P4-PING` 握手成功再发送配置，以兼容打开 USB 串口时设备复位的情况。按实际端口替换示例端口。
同机存在其他 Espressif 设备时，必须确认选择 P4，不能仅依据 VID/PID 判断。

底层接口供维护工具使用，发送一行：

```text
P4-WIFI ssid=<URL编码的SSID>&password=<URL编码的密码>&name=<URL编码的设备名>
```

设备只返回 `P4_WIFI_OK` 或 `P4_WIFI_ERROR`，不回显内容。SSID 非空、最长 32 字节；密码为空或 8–63 字节；名称为 1–40 个英文字母、数字、空格、点、下划线或连字符。
输入超过 640 字节或包含 NUL 时，整行丢弃，直至下一行。该接口仅通过本机物理 USB 连接使用。

2026-11-25 在 P4 rev 1.0 Claw4 上验证：无效配置被拒绝；有效配置提交 NVS 后重启；设备从保存的配置连接局域网，mDNS 发现及 RTSP OPTIONS 返回 200。

同一 USB 接口还有以下维护命令，每条独占一行：

| 命令 | 返回 | 用途 |
| --- | --- | --- |
| `P4-PING` | `P4_PONG` | 确认应用已启动、命令通道可用 |
| `P4-PAIR` | `P4_PAIR_OK` | 重开三分钟蓝牙配对窗口 |
| `P4-STATUS` | `P4_STATUS {JSON}` | Wi-Fi/IP、BLE 就绪、AirPlay 阶段、解码/显示/丢帧计数、解码错误 |
| `P4-MODE smooth` | `P4_MODE_OK` | 保存并切换快速同步档，重新发布 AirPlay 服务 |
| `P4-MODE sharp` | `P4_MODE_OK` | 保存并切换清晰档，重新发布 AirPlay 服务 |

切档不重启主控，但会结束旧 AirPlay 会话；在 Mac 连接带 `FastSync` 或 `Sharp` 后缀的名称。非法档位或 NVS 保存失败返回 `P4_MODE_ERROR`。状态 JSON 的 `profile` 为 `smooth` 或 `sharp`；`decoded` 只统计传到 UI 的输出，不包含主动跳过显示的解码图片。

最新图片优先管线另提供 `decoded_outputs`（复制成功的 I420 输出数）、
`replaced_outputs`（待显示旧图片被新图片覆盖的次数）、`frame_age_ms`（上次成功转换
的解码快照到 UI 的耗时）。最后一项不包含 Mac 发送、网络排队和面板扫描时间，
不能当作端到端延迟。

`P4_PAIR_OK` 只表示收到请求，实际广播和配对结果以日志为准。
`airplay=streaming` 只表示收到有效压缩数据；`decoded`、`shown` 增长且屏幕有图像才算实际出图。
调试时保持同一个串口句柄打开，通过它发送这些命令；反复关闭再打开串口可能使 P4 复位并中断连接。
