# Claw4 本地安装与验证（2026-11-25）

## 当前结果

源码已下载至 `/Users/zhuzhe/Workspace/esp-display`，本地 Git 使用 `main` 分支。
MetalioClaw4 已安装 P4 Native Display，并通过 USB 将用户指定的 `z-home` 配置保存到 NVS。
设备在自动重启后取得 `192.168.199.53`，Mac 发现 `MetalioClaw4 Display`，RTSP OPTIONS 返回 `RTSP/1.0 200 OK`。
以上验证了配置持久化、联网和服务可达，尚未验证真实 Mac 视频、帧率或键鼠交互。

用户报告首次启动时四周明显发亮，后来确认屏幕恢复。未修改面板电压、gamma、时序或驱动寄存器，不能把恢复称为代码修复。

## 来源与版本

- 来源：https://p4-native-display.zhuzhe1983.chatgpt.site/
- 原始 ZIP：https://p4-native-display.zhuzhe1983.chatgpt.site/downloads/p4-native-display-source-v0.1.0.zip
- ZIP SHA-256：`ce10dcbf9f15c3ea3f0a65972f6db85482d0908aef1c07384d78885094baec41`，与网站校验文件一致。
- 原始源码提交：`08f60e8`。导入时 `SOURCE_SHA256.json` 的 229 个文件均通过校验；该清单保留为原始版本依据，后续改动由 Git 记录。
- 原始 ZIP 与校验文件保存在忽略目录 `.build/downloads/`。
- 本地修改：修正 macOS 运行时测试的 `strlcpy` 宏冲突；新增 USB 配网，复用网页配网的 NVS 保存逻辑。

## 构建与镜像

```sh
source /Users/zhuzhe/Workspace/esp-agent/.build/toolchains/esp-idf-v5.5.4/export.sh
python tools/build.py metalio_claw4 build
```

使用 ESP-IDF 5.5.4、NV3051F、32 MB Flash、早期 P4 revision 0.x/1.x 配置。
当前安装镜像为 `firmware/build-metalio_claw4/p4_native_display.bin`：

- 大小：1,683,424 字节。
- SHA-256：`afde8abbb4a1402e61a4b1d70f9e26883cf275ebc302f5b948616000f7cd6fc9`。
- 本机安装副本和验证清单：`.build/device-20261125/usb-update/`。
- 首次下载版镜像为 1,674,256 字节，SHA-256 `2cdf0a76aa34b3a7465c510c3f2c78d811b6950da00221369ae533e75328956b`，后来被 USB 配网版替换。

## 设备与写入范围

- P4 串口：`/dev/cu.usbmodem1101`，MAC `30:ed:a0:e1:8d:4b`。
- 实际芯片：ESP32-P4 rev 1.0，Flash 32 MB，PSRAM 32 MB；安全启动和 Flash 加密均未启用。
- 实际分区表在 `0x8000`，MD5 有效；`ota_0` 为 `0x200000`、12 MiB，`ota_1` 为 `0xe00000`、12 MiB。
- 两份 OTA 记录 CRC 均有效，较新 sequence 2、state VALID，选择 `ota_1`。
- 原 `ota_0` 为 `xiaozhi` 2.0.5（IDF 5.5.4）；原 `ota_1` 为 `esp_agent` fdb706f-dirty（IDF 6.0.2），包含 NV3051F 驱动。
- 当前只更新 `ota_1`：镜像区间 `0xe00000–0xf9afe0`，末地址不含；Flash 扇区擦写范围 `0xe00000–0xf9b000`，末地址不含。
- 没有使用工程默认的整套 `idf.py flash`，没有改写 bootloader、分区表、原厂 `ota_0`、C5 或 eFuse。NVS 仅由运行中的配网功能按用户要求保存配置。

## 备份与校验

完整原机备份：`.build/device-20261125/before-flash.bin`，33,554,432 字节。
SHA-256：`02fc988d40dfb85c0f1694a17ea1b9613f7b4efdfc4b871061cc0f6fb08db4d0`。
该目录权限为 700，备份和运行日志不提交 Git；备份可能含用户配置。

首次 esptool 5.3 连续读取中断后，使用 SDK 配套 esptool 4.12 按 1 MiB 分块完成备份，每块通过设备返回的摘要校验，拼接后又通过整片设备 MD5 对比。
首次应用写入后逐字节读回一致，并在启动前对完整 32 MiB 与仅替换应用区的预期镜像做 MD5 校验，证明其他区域未变。
USB 配网版本再次写入后逐字节读回一致，写前新备份的前 2 MiB 在写后也通过 MD5 校验，覆盖 bootloader、分区表、NVS、OTA 数据及 model。

原机 bootloader 的附加 SHA-256 在写入前就与实际数据不匹配；ROM 会打印 `SHA-256 comparison failed` 后继续启动。
该 bootloader 始终保留原样。这个既存警告与新应用的写入校验不同，新应用读回校验通过。

## 运行与配网证据

启动日志确认 32 MB / 200 MHz PSRAM、720×720 NV3051F、GT911 地址 0x5d、C5 SDIO、Wi-Fi 和 BLE HCI 初始化成功。
USB 配网测试先发送无效配置并得到 `P4_WIFI_ERROR`，再发送有效配置并得到 `P4_WIFI_OK`；自动重启后日志显示 STA 连接、取得 IP、AirPlay 与 RAOP 服务注册。采样未见 panic/abort。

- `.build/device-20261125/startup-normal-reset.log`：首次启动。
- `.build/device-20261125/usb-wifi-runtime.log` 与 `usb-wifi-result.json`：USB 保存及重启联网。
- `.build/device-20261125/network-check.json`：RTSP OPTIONS 200。
- `.build/claw4-usb-build.log`、`.build/runtime-usb-tests.log`：当前构建与运行时回归通过。
- 原始解码、协议边界（含 20,000 个畸形输入）、输入报告测试通过；原始主机综合日志在 `.build/host-tests.log`，macOS 修正后的运行时通过日志在 `.build/runtime-tests.log`。

串口维护应在打开前将 DTR、RTS 设为 False，避免把 P4 留在下载/复位状态。曾有一次普通 RTS 复位未退出下载 stub；释放 BOOT 对应线路后正常启动成功。
配置脚本与观察结束均关闭了串口；临时 OpenOCD 与 Bonjour 查询进程也已退出。

## USB 连接和亮边诊断边界

最初只有另一个 ESP32-S31（MAC `1c:29:04:d0:b3:96`）及多级 HUB 中的 `1a86:8091`，没有 Claw4 主控。
对该 HUB 启用端口供电并进行标准端口复位后，下游连接位出现，但 macOS 未创建串口。
用户更换线缆并移至直接链路后，P4、蓝牙串口与 4G 一起枚举，才完成上述安装。

亮边出现时取得了部分当前扫描缓冲（369 行），上方 8 行及已读取两侧像素均为深色 BGR `(24,20,16)`，没有 UI 亮边；底边未读到，不能声称完成了整屏像素核对。
NV3051F 驱动与固定上游一致，DPI 实际 48 MHz、H 44/2/46、V 14/2/16，也与原固件一致。
长时间停留下载模式或 USB 复位后的面板状态是待验证方向，尚未确定根因。
一次较大的 JTAG 读屏超时后，已单独恢复 CPU 并退出调试器；热点配网两次未保存成功，改用 USB 配网后成功。Mac 已恢复连接 `z-home`。
