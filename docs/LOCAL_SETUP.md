# Claw4 本地安装与验证（2026-11-25）

## 当前结果

源码已下载至 `/Users/zhuzhe/Workspace/esp-display`，本地 Git 使用 `main` 分支。
MetalioClaw4 已安装 P4 Native Display，并通过 USB 将用户指定的 `z-home` 配置保存到 NVS。
设备在自动重启后取得 `192.168.199.53`，Mac 发现 `MetalioClaw4 Display`，RTSP OPTIONS 返回 `RTSP/1.0 200 OK`。
后续已完成真实 Mac 的 AirPlay 配对、视频解码/显示及 BLE 加密和报告订阅。
用户确认键盘初始化测试通过、鼠标能移动；Mac 实际收到左键按下/抬起。
超时修正版曾持续显示数百帧，但用户报告延迟不可接受；实测积压约 2–3 秒。
当前已安装默认快速同步档（FastSync），加入低分辨率请求、两帧队列及跳过过时输出。
设备 `/info` 的 360×360 与独立身份已经核实，新版 Mac 收流和实际延迟仍待重连验证。
720×720 原生桌面和绑定副屏的绝对触控仍未完成；本机 Mac 选择的是 800×600。
本地 `origin` 为 `git@github.com:zhuzhe1983/esp-airplay-display-extend-with-ble-hid.git`；按用户要求未推送。

用户报告首次启动时四周明显发亮，后来确认屏幕恢复。未修改面板电压、gamma、时序或驱动寄存器，不能把恢复称为代码修复。

## 来源与版本

- 来源：https://p4-native-display.zhuzhe1983.chatgpt.site/
- 原始 ZIP：https://p4-native-display.zhuzhe1983.chatgpt.site/downloads/p4-native-display-source-v0.1.0.zip
- ZIP SHA-256：`ce10dcbf9f15c3ea3f0a65972f6db85482d0908aef1c07384d78885094baec41`，与网站校验文件一致。
- 原始源码提交：`08f60e8`。导入时 `SOURCE_SHA256.json` 的 229 个文件均通过校验；该清单保留为原始版本依据，后续改动由 Git 记录。
- 原始 ZIP 与校验文件保存在忽略目录 `.build/downloads/`。
- 本地修改：macOS 测试兼容、USB 配网及诊断命令、720×720 显示描述与全屏滑动控制条、HTTP 控制请求、蓝牙广播串行化，以及早期 P4 的软件 ECC 配置。

## 构建与镜像

```sh
export PATH="/Users/zhuzhe/.espressif/python_env/idf5.5_py3.11_env/bin:$PATH"
source /Users/zhuzhe/Workspace/esp-agent/.build/toolchains/esp-idf-v5.5.4/export.sh
python tools/build.py metalio_claw4 build
```

使用 ESP-IDF 5.5.4、NV3051F、32 MB Flash、早期 P4 revision 0.x/1.x 配置。
当前安装镜像为 `firmware/build-metalio_claw4/p4_native_display.bin`：

- 大小：1,726,080 字节。
- SHA-256：`cd0822abfe0c46ff5cd469df4b72afea269247166acc14ce21c15c0e88d7075a`。
- 本机安装副本和验证清单：`.build/device-20261125/fastsync-update/`。
- 该诊断镜像显式启用 `P4_BLE_CRYPTO_PROBE=ON`，只记录测试结果及返回码。普通新构建目录默认关闭，切换说明见 [实物探针](../tests/on_device/README.md)。
- USB 配网版 `afde8ab…`、首次全屏版 `449b…`、广播修正版 `819d…` 及加密对照镜像留在本机忽略目录，用于追溯。
- 首次下载版镜像为 1,674,256 字节，SHA-256 `2cdf0a76aa34b3a7465c510c3f2c78d811b6950da00221369ae533e75328956b`，后来被 USB 配网版替换。

## 设备与写入范围

- P4 串口：`/dev/cu.usbmodem1101`，MAC `30:ed:a0:e1:8d:4b`。
- 实际芯片：ESP32-P4 rev 1.0，Flash 32 MB，PSRAM 32 MB；安全启动和 Flash 加密均未启用。
- 实际分区表在 `0x8000`，MD5 有效；`ota_0` 为 `0x200000`、12 MiB，`ota_1` 为 `0xe00000`、12 MiB。
- 两份 OTA 记录 CRC 均有效，较新 sequence 2、state VALID，选择 `ota_1`。
- 原 `ota_0` 为 `xiaozhi` 2.0.5（IDF 5.5.4）；原 `ota_1` 为 `esp_agent` fdb706f-dirty（IDF 6.0.2），包含 NV3051F 驱动。
- 当前只更新 `ota_1`：镜像区间 `0xe00000–0xfa5680`，末地址不含；Flash 扇区擦写范围 `0xe00000–0xfa6000`，末地址不含。
- 没有使用工程默认的整套 `idf.py flash`，没有改写 bootloader、分区表、原厂 `ota_0`、C5 或 eFuse。运行中的应用在 NVS 保存配网、蓝牙绑定、AirPlay 身份和显示档位。

## 备份与校验

完整原机备份：`.build/device-20261125/before-flash.bin`，33,554,432 字节。
SHA-256：`02fc988d40dfb85c0f1694a17ea1b9613f7b4efdfc4b871061cc0f6fb08db4d0`。
该目录权限为 700，备份和运行日志不提交 Git；备份可能含用户配置。

首次 esptool 5.3 连续读取中断后，使用 SDK 配套 esptool 4.12 按 1 MiB 分块完成备份，每块通过设备返回的摘要校验，拼接后又通过整片设备 MD5 对比。
首次应用写入后逐字节读回一致，并在启动前对完整 32 MiB 与仅替换应用区的预期镜像做 MD5 校验，证明其他区域未变。
后续各应用更新均逐字节读回一致，写前新备份的前 2 MiB 在写后也通过 MD5 校验，覆盖 bootloader、分区表、NVS、OTA 数据及 model。

原机 bootloader 的附加 SHA-256 在写入前就与实际数据不匹配；ROM 会打印 `SHA-256 comparison failed` 后继续启动。
该 bootloader 始终保留原样。这个既存警告与新应用的写入校验不同，新应用读回校验通过。

## 运行与配网证据

启动日志确认 32 MB / 200 MHz PSRAM、720×720 NV3051F、GT911 地址 0x5d、C5 SDIO、Wi-Fi 和 BLE HCI 初始化成功。
USB 配网测试先发送无效配置并得到 `P4_WIFI_ERROR`，再发送有效配置并得到 `P4_WIFI_OK`；自动重启后日志显示 STA 连接、取得 IP、AirPlay 与 RAOP 服务注册。采样未见 panic/abort。

- `.build/device-20261125/startup-normal-reset.log`：首次启动。
- `.build/device-20261125/usb-wifi-runtime.log` 与 `usb-wifi-result.json`：USB 保存及重启联网。
- `.build/device-20261125/network-check.json`：RTSP OPTIONS 200。
- `.build/claw4-session-recovery-build.log`、`.build/runtime-recovery-render.log`：当前构建与运行时回归通过，包括像素完整性、边缘控制条、限流、跨会话取消和末帧保留/恢复。
- `.build/input-tap-tests.log`、`.build/edid-protocol-tests.log`：轻点容差、HID、EDID 格式/校验和、H.264/plist 与 20,000 个畸形输入通过。
- `.build/ui-fullscreen/`：主机渲染的全屏、控制条及键盘画面，明确标为诊断背景，不能作为 Mac 实际出图证据。
- 原始解码、协议边界（含 20,000 个畸形输入）、输入报告测试通过；原始主机综合日志在 `.build/host-tests.log`，macOS 修正后的运行时通过日志在 `.build/runtime-tests.log`。

串口维护应在打开前将 DTR、RTS 设为 False，避免把 P4 留在下载/复位状态。曾有一次普通 RTS 复位未退出下载 stub；释放 BOOT 对应线路后正常启动成功。
配置脚本与观察结束均关闭了串口；临时 OpenOCD 与 Bonjour 查询进程也已退出。

## Mac 连接诊断

Mac 为 Apple M5 Max（Mac17,7），macOS 27.2.1（26B131），IP `192.168.199.177`。
实测证据在 `.build/device-20261125/connect-debug/`，均不提交 Git。

19:01 的 `connection-819d.log` 记录 Mac 完成 legacy pairing、FairPlay、两次 SETUP、RECORD
及视频 TCP 连接，随后持续 feedback，但解码和显示计数均为零。
`mac-display-start.log` 显示 Mac 读到 720×720，却在应用 10 Hz 模式时
`Fail to create SLVirtualDisplaySettings`；还提示物理尺寸为零。
当前固件改为 30 Hz、71×71 mm，`display-geometry-check.json` 已从实物返回值核实。
物理尺寸由厂商 3.95 英寸方屏对角线换算；后续加入标明 PND 开发设备的 EDID，见下文。

同次蓝牙连接在设备端返回加密状态 17；Mac 记录对端发送 SMP 失败原因 8。
板上公开测试向量定位到：TinyCrypt 的 DHKey 和随机密钥生成均返回 17；
mbedTLS 硬件 ECC 返回 0 但结果错误；关闭硬件 ECC 后 `rc=0 match=1`，随机密钥生成返回 0。
对应 `crypto-tinycrypt-runtime.log`、`mbedtls-connection.log`、`software-connection.log`。
这一步验证了软件 ECC 的数学结果；后续实际连接结果如下。

后续实测已经出现 `encryption status=0 encrypted=1`、报告订阅 `mask=0xf` 和
`Keyboard and mouse ready`；更新应用后也自动重连。
`mac-mouse-buttons.log` 只监听本设备的鼠标按钮，记录到两组左键 1/0，未采集键盘内容。

`edid-connection.log` 的显示计数从 0 增至 38、60、75，丢帧保持零，实际编码为 720×540。
旧版在 Mac 仍每两秒发送 feedback 时触发 `mirror: incomplete packet timed out`，
随后关闭视频连接并进入 `no-video`，解释了用户看到的画面消失和需手动重连。
当前版本以“30 秒无接收进度且最近 10 秒无所属控制连接请求”判定停滞；
等待新帧时保留末帧、显示等待提示，收到新解码帧后自动恢复全屏。

Mac 已确认收到 EDID，`display-edid-check.json` 记录 128 字节和有效校验和。
系统仍把 720×720 原生模式标为不支持：公开设置 API 返回 1001，displayplacer 指定原生模式后
也回到 800×600，测试后恢复了原副屏排列。没有修改全局显示器偏好文件或安装常驻驱动。
用户要求移动光标时，实际按副屏边界将其移到中央，API 返回 0 且位置读回一致；
可复用 `tools/focus_display.swift`，这只是一次定位，不将普通 BLE 相对鼠标绑定到副屏。

## 快速同步与清晰两档

控制条第四个按钮在 `FASTSYNC` / `SHARP` 间切换；NVS 保存选择。
Mac 分别连接 `MetalioClaw4 Display FastSync` 或 `MetalioClaw4 Display Sharp`。
切换会重新发布 AirPlay，不重置 Wi-Fi 或蓝牙绑定。USB 也可发送 `P4-MODE smooth` /
`P4-MODE sharp`，返回 `P4_MODE_OK`，状态 JSON 的 `profile` 显示请求的档位。

旧版的板上采样：720×540 流解码约 90–130 ms、转换约 110 ms、发布约 25 ms，
12 帧队列等待约 2–3 秒；记录在 `connect-debug/modes-connection.log`。
当时虽已请求 360×360，Mac 日志仍报告旧 720×720 接收能力，实际仍发 720×540。
因此新版的两档采用独立、持久的 AirPlay 设备 ID、公钥、PI 和显示 UUID。
快速同步的 deviceID 为本地管理的 `32:ED:A0:E1:8D:4A`，不修改无线接口地址。

当前压缩队列为两帧。积压时距上次发布结束不足 150 ms 的输出跳过 RGB 转换及显示，
仍完成参考帧解码。最近邻放大复用重复源像素与源行，减少颜色运算。
这属于抽帧显示，不能保证 CPU 在所有复杂画面下都能实时追赶。

`.build/claw4-latency-build.log`、`.build/host-fastsync-tests.log`、
`.build/ui-fastsync-render.log` 记录编译与测试通过，包含真实 AVC 的 B 帧重排、
省略输出后的参考帧完整性及两档按钮渲染。
`connect-debug/fastsync-descriptor.json` 记录实物返回 `360×360 / 30 fps`、
有效 EDID 校验和及独立身份；`fastsync-connection.log` 记录启动、Wi-Fi 和 BLE 自动恢复。
`fastsync-mode-switch.json` 记录实物双向切档通过，恢复快速同步后公钥保持一致，
两档的公钥和广播身份各自独立，最终保留快速同步档。
这些结果不等于已经测得新版端到端延迟，仍需 Mac 连接该新名称后测量。

## USB 连接和亮边诊断边界

最初只有另一个 ESP32-S31（MAC `1c:29:04:d0:b3:96`）及多级 HUB 中的 `1a86:8091`，没有 Claw4 主控。
对该 HUB 启用端口供电并进行标准端口复位后，下游连接位出现，但 macOS 未创建串口。
用户更换线缆并移至直接链路后，P4、蓝牙串口与 4G 一起枚举，才完成上述安装。

亮边出现时取得了部分当前扫描缓冲（369 行），上方 8 行及已读取两侧像素均为深色 BGR `(24,20,16)`，没有 UI 亮边；底边未读到，不能声称完成了整屏像素核对。
NV3051F 驱动与固定上游一致，DPI 实际 48 MHz、H 44/2/46、V 14/2/16，也与原固件一致。
长时间停留下载模式或 USB 复位后的面板状态是待验证方向，尚未确定根因。
一次较大的 JTAG 读屏超时后，已单独恢复 CPU 并退出调试器；热点配网两次未保存成功，改用 USB 配网后成功。Mac 已恢复连接 `z-home`。
