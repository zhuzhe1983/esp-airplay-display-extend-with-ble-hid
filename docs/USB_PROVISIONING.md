# USB 配网

当电脑无法连接设置热点时，可通过 P4 的 USB Serial/JTAG 接口写入 Wi-Fi 配置。
本功能与网页配网共用 `p4_network` NVS 命名空间，保存后设备自动重启。密码不编译进固件，也不会回显到串口日志。

在固件完成启动后运行，需要 Python 与 pyserial：

```sh
python tools/configure_wifi.py --port /dev/cu.usbmodem1101 --ssid "your-network" --name "MetalioClaw4 Display"
```

脚本会单独提示输入密码，不将密码作为命令行参数。按实际端口替换示例端口。
同机存在其他 Espressif 设备时，必须确认选择 P4，不能仅依据 VID/PID 判断。

底层接口供维护工具使用，发送一行：

```text
P4-WIFI ssid=<URL编码的SSID>&password=<URL编码的密码>&name=<URL编码的设备名>
```

设备只返回 `P4_WIFI_OK` 或 `P4_WIFI_ERROR`，不回显内容。SSID 非空、最长 32 字节；密码为空或 8–63 字节；名称为 1–40 个英文字母、数字、空格、点、下划线或连字符。
输入超过 640 字节或包含 NUL 时，整行丢弃，直至下一行。该接口仅通过本机物理 USB 连接使用。

2026-11-25 在 P4 rev 1.0 Claw4 上验证：无效配置被拒绝；有效配置提交 NVS 后重启；设备从保存的配置连接局域网，mDNS 发现及 RTSP OPTIONS 返回 200。
