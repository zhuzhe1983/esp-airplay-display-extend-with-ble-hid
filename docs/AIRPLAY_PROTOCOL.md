# 原生 AirPlay 接收组件：实现范围和验收边界

这个组件为 ESP32-P4 实现局域网内的 **legacy AirPlay 屏幕镜像接收协议**。Mac 端的目标入口是系统“屏幕镜像”菜单，不需要安装 HoppScreen 或配套发送程序。项目目前属于研发原型：Claw4 已实际完成 Mac 配对、FairPlay、SETUP 和视频 TCP 连接，但此前的 10 Hz 显示模式在 WindowServer 创建时失败；实际出图结果见 [本机安装记录](LOCAL_SETUP.md)。不能把成功编译、Bonjour 可发现或握手完成写成“已经投屏成功”。

它不是 Sidecar。将 Bonjour 名称设成“iPad”不会实现 Sidecar 的身份、边栏、触控栏、Apple Pencil 或触摸回传。独立 USB/BLE HID 的实现与本组件分开，必须另测 macOS 的指针行为以及扩展屏的坐标映射。

## 来源和许可证

- 接收核心：[`onehumanbeing/esp32-iphone-agent`，commit `90f450e`](https://github.com/onehumanbeing/esp32-iphone-agent/tree/90f450ea7c2741901a2b4102c8d209c930961912/hardware/hid_bridge/src/airplay)。原作者 Henry Yuan；保留 `LICENSE-UPSTREAM-MIT`、`NOTICE-UPSTREAM` 及源码里的 UxPlay/RPiPlay/PlayFair 版权声明。
- 协议对照：[`FDH2/UxPlay`，commit `48f5ce4`](https://github.com/FDH2/UxPlay/tree/48f5ce47c9ef694afccfac907c9e8828329657fe/lib)。它具备桌面系统上的原生 macOS AirPlay 接收实践，但不等于这个 MCU 移植已经兼容相同发送端。
- AirPlay/PlayFair 部分为 GPL v3；`COPYING` 和 `core/playfair/LICENSE.md` 提供完整许可证。二进制 plist、原始 H.264 分帧辅助代码来自上游的 MIT 部分。最终链接固件需要按适用的 GPL 条件提供对应源码；组件依赖保留各自许可证。

上游 ESP32 项目的视频架构是“接收/解密后将 H.264 转发给手机解码”，并未证明 P4 自身可以解码并显示完整 Mac 桌面。其 [issue #46](https://github.com/onehumanbeing/esp32-iphone-agent/issues/46) 记录了 Mac 完成握手后没有视频的情况。这是当前移植的重要风险，不能用手机作为发送端的成功记录替代 Mac 验收。

## 实际接入的链路

| 阶段 | 组件行为 | 不能据此得出的结论 |
|---|---|---|
| 服务发现 | 发布 `_airplay._tcp`、`_raop._tcp` 和与 legacy mirror 匹配的 TXT 字段 | 可见设备不代表认证、视频或扩展模式成功 |
| 控制连接 | TCP 7000 上的有界 HTTP/RTSP 解析、`/info`、`OPTIONS`、参数、反馈、`RECORD`、`TEARDOWN` | 返回 200 不是显示成功 |
| 配对与密钥 | 真实 Ed25519/X25519 legacy pair-setup/pair-verify；FairPlay v3/PlayFair；SHA-512 派生会话密钥 | 不包含 PIN/SRP 配对、完整 AirPlay 2 或 DRM 播放 |
| 首次 SETUP | 验证 `ekey` / `eiv`、建立会话、启动 NTP/音频 UDP 端口 | 时钟可交互不代表 Mac 将发送视频 |
| 视频 SETUP | type 110、`streamConnectionID`、TCP 7100 listener、派生 AES-CTR 流密钥 | TCP 已连接仍可能没有 SPS/PPS 或图像 |
| 镜像数据 | 128 字节包头、有界载荷、AES-CTR 解密、AVCC 检查和 Annex B 转换 | 合法 H.264 AU 不保证解码器接受其 profile 或生成像素 |
| 应用回调 | IDR 前附最近 SPS/PPS，给出相对时间戳和 keyframe 标志 | 必须另外记录解码帧与屏幕提交帧才能确认真实画面 |

端口固定为：TCP 7000 控制、TCP 7100 镜像数据；UDP 7010 NTP、7011 音频数据、7012 音频控制。应用必须先完成 NVS、网络接口与有效 IPv4 地址初始化。当前不实现 AWDL/点对点 AirPlay、IPv6 接收、跨网段发现、中继服务或云连接。

## 与上游 iPhone 移植的关键差异

1. **时钟更早启动。** UxPlay 在包含 `ekey/eiv` 的第一次 SETUP 就启动 NTP 和音频传输；上游 MCU 移植等到 type 110 SETUP 才启动。这里把 UDP 传输提前，以避免在未监听的 timingPort 上返回就绪信息。这个修正是对齐已知实现，尚未证明它解决了上游 Mac 无视频问题。
2. **配对状态检查。** 请求了 legacy pairing 的发送端必须完成 pair-verify，才能将 ECDH secret 混入 FairPlay 解出的 AES key；没有配对的 legacy 流保留不混入 secret 的路径。无效签名和无效密钥长度会明确失败。
3. **失败状态可见。** 第一次等待视频超过 5 秒仍没有完整可转发的 SPS/PPS + IDR 时进入 `NO_VIDEO`，不会用心跳包将状态置为成功。图像静止后没有新 AU 本身不视为失败。
4. **保留音频协议入口。** 当前不播放声音，音频 UDP 包被接收并丢弃。没有通过随意清除音频 feature 来宣称“视频模式兼容”，因为部分 AirPlay 发送端依赖音频/时钟协议保持会话。视频投屏时声音路由仍需实机检查。
5. **本地显示回调。** 原项目把压缩流送回 iPhone；本项目把完整压缩 AU 交给本地解码队列。无需 iPhone relay，也不使用浏览器 WebCodecs。

## 公共 API 和线程约定

```c
esp_err_t airplay_receiver_start(const airplay_receiver_config_t *config);
esp_err_t airplay_receiver_stop(void);
airplay_receiver_state_t airplay_receiver_get_state(void);
```

`config` 包含 `name`、`width`、`height`、`max_fps`、`width_mm`、`height_mm`、`frame_cb`、`state_cb`、`ctx`。物理宽高以毫米表示，未知时同时为零；不能只填写一边。组件是单实例；start/stop 由应用的同一控制任务调用，不能在回调内调用，也不能并发调用。设备名称最多 48 个 UTF-8 字节；组件会复制名称。

`frame_cb` 的 `airplay_receiver_frame_t` 提供 `data`、`len`、`pts_us`、`keyframe`。数据为 Annex B 格式：IDR 的开头已拼接最近的 SPS/PPS；`pts_us` 是 sender 的 32.32 时间戳换算后相对本会话首 AU 的微秒值。它不是已经做 NTP 偏移校准的本地墙钟。应用应以低延迟显示为目标，不把它误当作完成 A/V 同步的时钟。

所有指针只在当前回调内有效。**必须同步复制进有界队列后尽快返回**，不能 retain 原 buffer，也不能在镜像网络任务里执行整帧软件解码。`frame_cb` 与 `state_cb` 可能来自不同任务，因此应用状态/队列必须线程安全。队列满了不能继续把失去参考帧的 P/B 帧送进原解码器；应用应清空积压，重置解码状态并等待新的 IDR。如果发送端长时间不再发 IDR，需要重新开始系统镜像会话，当前未实现通用关键帧请求。

| 状态 | 含义 |
|---|---|
| `STOPPED` | 接收任务停止 |
| `ADVERTISING` | 服务已发布，或上一会话已结束 |
| `CONNECTING` | 正在 legacy pairing / 密钥与时钟初始化 |
| `WAITING_VIDEO` | 等待数据连接、SPS/PPS、IDR；或丢帧/格式变化后等待新 IDR |
| `STREAMING` | 已验证并解密出完整 H.264 AU，并调用 frame callback；未声称屏幕已出图 |
| `NO_VIDEO` | 视频 SETUP 后超时，仍不能输出完整有效的初始帧 |
| `ERROR` | 明确不支持的格式、分配失败或启动/停止故障 |

应用应另外展示“已解码帧数”“已提交屏幕帧数”与当前 profile/分辨率。首次屏幕显示前不能显示“投屏成功”。

## 输入和资源上限

- 同时最多 3 个 RTSP TCP 连接、1 个镜像会话；不同发送端不能抢占已建立的会话。镜像 TCP 和音频/NTP UDP 的来源受已建立会话的 IP 约束。
- RTSP 请求总长最多 64 KiB，头部最多 8 KiB、24 个字段；拒绝重复 Content-Length、非法十进制长度、chunked/Transfer-Encoding 和整数溢出。未完成请求有 5 秒期限，闲置且未拥有会话的控制连接有 30 秒期限。
- 镜像载荷最多 2 MiB；未完成包按接收进度和所属控制连接活跃状态回收，超限不会继续分配。codec 配置最多 4 KiB，支持 avcC 内多个 SPS/PPS，NAL length 字段必须为 4 字节。
- binary plist 对象数最多 4096；偏移、长度、对象表、容器项数均用不溢出的边界检查。输出 plist 最多 64 KiB。
- 最大镜像 buffer、解密副本、恢复缓存、带 SPS/PPS 的 IDR 可能合计接近 8 MiB PSRAM；另需解码器参考帧、解码/渲染队列及 LCD framebuffer。必须基于真实流记录 PSRAM 最小剩余值，不能只按压缩平均码率估内存。
- 建议 `CONFIG_LWIP_MAX_SOCKETS` 至少为 20，为 mDNS、应用服务和网络组件保留容量。RTSP 和镜像任务各用 12 KiB 内部 RAM stack。
- 接收身份通过 NVS `p4_airplay/ed25519_seed` 持久保存；日志不打印私钥或会话 AES key。此原型没有 PIN/白名单配对 UI，因此在同一局域网内有访问能力的发送端可以尝试连接。

## H.264 profile 与性能必须单独验证

`width`、`height`、`maxFPS` 是 `/info` 里的接收端请求，发送端可能选择不同参数。本组件不会假装能用一个 TXT 位或修改 SPS profile 字节把 High/CABAC 视频变成 Baseline。

Claw4 的描述为 720×720、最高 30 fps、物理尺寸约 71×71 mm；后者由厂家标注的
[3.95 英寸方屏](https://github.com/CloudZao/MetalioClaw4#1-product-overview)对角线换算并取整。
显示描述包含自建的 EDID 1.4（开发设备标识 PND），只列出首选原生时序；
校验和、像素尺寸、物理尺寸及 30 Hz 时钟有主机测试。2026-11-25 的 Mac 日志已经读到
720×720，但旧版的 10 Hz 模式不能创建 `SLVirtualDisplaySettings`，并提示零物理尺寸无效，
因此同时修正模式上限和物理尺寸。该 Mac 随后创建了扩展屏，但仍选用 800×600
逻辑桌面、发送 720×540 视频；720×720 原生模式被标为不支持，显式选用也回落。
EDID 已被 Mac 识别为 present，不能因此宣称桌面已实现 720×720 的 1:1。

解码队列满时最多等待两秒，让 TCP 接收窗口限流，期间释放队列锁，连接重置可取消等待。
超过上限仍清理队列并等待 IDR，以维持内存边界与参考帧完整性。
视频包的超时改为接收进度停滞 30 秒且最近 10 秒没有所属控制连接的有效请求；
持续收到 Mac 的 RTSP feedback 时保留半包，避免静态桌面或限流写入触发误断连。

UxPlay 的 [Mac 样例 SPS/PPS](https://github.com/FDH2/UxPlay/issues/73#issuecomment-1079728540) 包含 High profile、CABAC 与 8×8 transform，因此只接 Espressif constrained-baseline 解码器不够。该历史样例用于证明存在这种格式，不代表所有当前 macOS 版本都输出相同码流。

本项目的解码组件需要独立验证真实 SPS/PPS、参考帧数、实际编码尺寸、High/CABAC 支持、峰值内存及 CPU 占用。ESP32-P4 的 H.264 硬件编码器不能当作 H.264 解码器使用。即使加入支持 High 的软件解码器，也不能从“支持格式”直接推导出 720×720 或 720×1280 的可用帧率。

## 已做的检查与上板验收

组件内 `tests/run_host_tests.sh` 检查 AVCC/avcC 的有效与截断输入、错误 NAL 长度的原子拒绝、plist 往返、溢出对象表及 20,000 个确定性畸形输入。它采用 AddressSanitizer 和 UndefinedBehaviorSanitizer；在本次受 ptrace 管理的执行环境中设置了 `ASAN_OPTIONS=detect_leaks=0`，仅关闭不兼容该环境的 LeakSanitizer。PlayFair 源文件做了 `-Wall -Wextra -Werror` 的主机语法检查。这些检查验证对应软件性质，不验证真实 AirPlay 认证成功率、无线链路或实板性能。

实板验收应记录 Mac 型号/macOS 版本、设备型号/硬件版本、发送端所选模式、实际 SPS profile/level、编码宽高、解码错误数、已显示帧数、帧率和 PSRAM 最低剩余值。顺序为：

1. Mac 原生菜单发现设备，连上同一局域网的真实接收器。
2. 检查 pair-verify / FairPlay / SETUP 的真实日志；如果失败，保留失败阶段，不显示假成功。
3. 确认收到 SPS/PPS 与 IDR，日志中的 profile/level 来自实际视频。
4. 解码器产出像素并在物理屏幕显示 Mac 的动态画面；测试静止、滚动、分辨率切换和断连重连。
5. 在 macOS 提供相应选项的条件下，分别测试镜像与扩展显示；不能只测镜像就宣称扩展成功。
6. 独立测 HID 回传。普通相对鼠标回传不能直接宣称实现 Sidecar 的绝对触摸/Apple Pencil 行为。
7. 两种目标硬件分别测至少一段持续会话，记录热量、帧率、队列丢帧和内存；在达到目标体验之前保留“实验”标识。

若看到 `NO_VIDEO`，应先比对发送端握手和第一个 codec packet 是否存在；若 `STREAMING` 增长但已解码帧数为零，应检查解码器/profile/参考帧；已解码有增长但屏幕不更新则检查像素格式、stride、缩放、缓存一致性和 framebuffer 提交。按实际失败阶段定位，避免把显示屏驱动、协议认证与视频解码混成一个问题。
