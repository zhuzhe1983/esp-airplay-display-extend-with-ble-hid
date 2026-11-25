# 本地构建与设备检查（2026-11-25）

源码来自 https://p4-native-display.zhuzhe1983.chatgpt.site/ 的 v0.1.0 ZIP，下载地址：
https://p4-native-display.zhuzhe1983.chatgpt.site/downloads/p4-native-display-source-v0.1.0.zip

- 本地目录：`/Users/zhuzhe/Workspace/esp-display`
- Git 分支：`main`；原始源码提交：`08f60e8`
- ZIP SHA-256：`ce10dcbf9f15c3ea3f0a65972f6db85482d0908aef1c07384d78885094baec41`，与网站校验文件一致。
- 导入时 `SOURCE_SHA256.json` 中的 229 个文件均通过校验。该清单保留为原始版本依据，后续改动由 Git 记录。
- 原始 ZIP 与网站校验文件保存在忽略目录 `.build/downloads/`。

## 构建

本机已有 ESP-IDF v5.5.4 与 ESP32-P4 工具链：

```sh
source /Users/zhuzhe/Workspace/esp-agent/.build/toolchains/esp-idf-v5.5.4/export.sh
python tools/build.py metalio_claw4 build
```

构建通过。使用默认 NV3051F 面板、32 MB Flash、早期 P4 revision 0.x/1.x 配置。应用文件：

`firmware/build-metalio_claw4/p4_native_display.bin`

- 大小：1,674,256 字节。
- SHA-256：`2cdf0a76aa34b3a7465c510c3f2c78d811b6950da00221369ae533e75328956b`。
- 镜像检查：ESP32-P4，IDF v5.5.4，版本 0.1.0，支持芯片 revision v0.0–v1.99。
- 构建日志：`.build/claw4-build.log`。

## 主机检查

H.264 Baseline/Main/High、协议包边界与 20,000 个畸形输入、触控与 HID 报告测试通过。
运行时测试在 macOS 上遇到系统 `strlcpy` 宏与测试替身重定义冲突；在测试替身前取消已存在的宏后，UI、队列、解码恢复与屏幕呈现计数测试通过。固件代码未因此修改。

原始综合日志在 `.build/host-tests.log`，运行时修正后的通过日志在 `.build/runtime-tests.log`。

## 当前连接检查

截至本次初检，macOS 唯一外接串口 `/dev/cu.usbmodem101` 经 esptool v5.3.0 实际识别为 ESP32-S31 revision v0.0，MAC `1c:29:04:d0:b3:96`。用户确认 S31 是另一个设备；尚未发现 Claw4 的 P4 主控端口，未执行 Flash 写入。端口观察记录在 `.build/usb-detection.log`。

完整 USB 树还显示 `1a86:8091`（USB HUB），位于 `02144330`，通过 Corsair ST100 与多级集线器连接，当前没有子设备。该 VID/PID 与本机此前 Claw4 排查记录一致，可作为拔插对照线索；尚未通过本次拔插确认归属，不能将 HUB 本身当作 P4 主控。

Claw4 官方主控端口描述为 `USB JTAG/serial debug unit`，但必须进一步读取芯片身份，不能仅依赖端口名称。

连接排查：使用数据线直连、设备开机并正常重启；若使用虚拟 USB 磁盘，应先从电脑安全弹出并在设备停用。若存在明确标记的 BOOT/RESET，可按住 BOOT、短按 RESET，复位后松开 BOOT。P4 原理是复位时 GPIO35 为低；官方 Claw4 文档没有标注机身按键位置，不将电源键当作 BOOT，也不要求拆机短接。

- [Claw4 USB 与烧录说明](https://github.com/CloudZao/MetalioClaw4/blob/main/README.md#143-usb-debug-ports)
- [ESP32-P4 下载模式](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html#manual-bootloader)

P4 连接恢复后，应先核对芯片 revision、面板、Flash 与原机分区并备份，再决定写入范围。项目默认 `flash` 会写入 bootloader、分区表与应用；此命令当前未执行。C5 无线固件也未改写。

## 第二次连接排查（16:23–16:27）

用户确认 Claw4 正常显示桌面。`1a86:8091` HUB 路径从 `02144330` 改为 `02144220`，已离开 Corsair ST100，但仍经过多级 HUB。系统串口和 IORegistry 均仍只有已确认的 S31，没有 P4。

通过本机 libusb 对该 HUB 读取标准描述符和端口状态（没有卸载驱动、修改 USB 配置或操作其他 HUB）：

- HUB 描述符 `0929042000306400ff`，四个下游口；初次四口状态均为 `00000000`，未供电、未连接。
- 向该 HUB 四口发送标准 `SET_FEATURE(PORT_POWER)`，全部成功；端口 1、2、4 状态变为 `01010100`，报告已供电并检测到连接，端口 3 为 `00010000`。
- 仅对已连接的 1、2、4 口发送标准 `SET_FEATURE(PORT_RESET)`，全部成功；1、2 口状态 `03011100`，4 口 `03051100`，均报告已启用。
- 此后 macOS 仍未创建下游 USB 设备和串口。仅凭端口连接位无法确定下游芯片身份，仍不能烧录。
- 操作结束均关闭 libusb 句柄；未执行任何芯片 Flash 或 eFuse 写入。详细记录：`.build/usb-hub-recovery.log`。

下一项对照：临时拔下已正常枚举的 S31，将 Claw4 接到 S31 的同一根数据线及同一个电脑接口，以排除现有中间 HUB 链路。
