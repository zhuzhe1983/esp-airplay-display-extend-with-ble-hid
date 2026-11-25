# P4 视频解码：真实 OpenH264 软件路径

## 当前实现和验证结论

`firmware/components/video_decoder` 包含真正的 OpenH264 解码器与一个 C ABI
封装。输入为 **H.264 Annex B 的完整 access unit（AU）**，输出为按比例放入物理
屏幕、空白处填黑的 RGB565 帧。支持本项目限定范围内的 **Baseline、Main、High，
包括 CAVLC、CABAC 和 B 帧重排**。此路径在 CPU 上执行；它没有把 ESP32-P4 的
H.264 **编码**硬件当作解码器，也没有生成占位帧。

本次已经完成的验证是 Linux 主机上的真实解码。使用 FFmpeg/libx264 生成的三个
160×96、8 帧测试流，Main/High 均启用 CABAC、B 帧和每 AU 两个 slice；独立使用
FFmpeg 解码生成逐帧 RGB 参考采样。测试输出如下，数值范围为每色彩通道 0–255：

| 输入 | 实际输出帧 | 不同 RGB565 帧 | 对 FFmpeg 采样的平均绝对误差 | 最大采样误差 |
|---|---:|---:|---:|---:|
| Constrained Baseline / CAVLC | 8 | 8 | 1.356 | 9 |
| Main / CABAC / B 帧 | 8 | 8 | 1.429 | 30 |
| High / CABAC / B 帧 | 8 | 8 | 1.753 | 26 |

RGB565 量化、色度采样和独立转换器的整数舍入会产生少量 RGB 差异。测试也检查
参数集单独输入、帧顺序、EOS 排空、重置后重新解码、横竖屏适配与黑边、超尺寸、
参考帧数量上限、缺失 SPS/PPS、截断数据和 AU 长度上限。1920×1080 的参数集
测试确认允许编码所需的 1920×1088 宏块补齐；默认 1280×1280 配置会拒绝该输入。

同一套测试在 AddressSanitizer 构建下通过，未发现这批输入触发的内存越界。
执行环境的 ptrace 限制使 LeakSanitizer 无法运行，故该次使用
`ASAN_OPTIONS=detect_leaks=0`；没有把这项结果报告为内存泄漏验证。

2026-11-25 已在 P4 rev 1.0 Claw4 实际显示 Mac 视频，并对旧版 720×540 流测时：
单 AU 解码约 90–130 ms，RGB 转换约 110 ms，UI 发布约 25 ms；12 帧队列积压
约 2–3 秒。这些是管线分段采样，不是输入到屏幕的端到端延迟。新版快速同步
已上板，实际收流与性能仍待 Mac 重新连接验证；峰值内存尚未测量。

## 对外接口

头文件：`firmware/components/video_decoder/include/video_decoder.h`。

```c
typedef struct video_decoder video_decoder_t;

int video_decoder_init(const video_decoder_config_t *config,
                       video_decoder_t **out_decoder);
int video_decoder_push(video_decoder_t *decoder,
                       const uint8_t *annexb, size_t length);
int video_decoder_flush(video_decoder_t *decoder);
int video_decoder_reset(video_decoder_t *decoder);
void video_decoder_destroy(video_decoder_t *decoder);
const char *video_decoder_error_string(int result);
```

配置结构包括 `panel_width`、`panel_height`、`max_input_width`、
`max_input_height`、`max_access_unit_bytes`、`callback`、`user` 和可选 `should_present`。
后者在成功解码后、RGB 转换前运行；返回 false 时跳过该图片的转换和回调，
解码器内的参考图片保持完整。真实 Baseline/Main/High 测试流验证了连续省略
前四个输出后，其余四帧仍与 FFmpeg 的对应参考帧匹配，含 B 帧重排。
`callback(void *user, const video_decoder_frame_t *frame)` 在调用解码函数的
同一任务上同步运行。`frame` 含以下字段：

| 字段 | 含义 |
|---|---|
| `rgb565` | 全屏、主机字节序的 `uint16_t` RGB565 数据 |
| `width` / `height` | 物理屏幕分辨率 |
| `stride_bytes` | 每行字节数，当前等于 `panel_width * 2` |
| `view_rect.{x,y,width,height}` | 视频实际占据的屏幕矩形，黑边不在该矩形内 |
| `source_width` / `source_height` | 解码输出的可见视频像素尺寸 |
| `frame_number` | 本会话从 1 开始的输出帧序号 |

调用约定：

1. 同一 handle 的全部操作由一个解码任务串行调用，回调不可重入该 handle。
2. 一次 `push` 输入一个完整 AU。SPS/PPS 可作为独立的参数包提前输入。AVCC 的
   NAL 长度前缀需要接收器先转换为 Annex B 的 `00 00 00 01` 起始码。
3. 返回 0 表示接受该包，不保证立即有帧。参数包与 B 帧重排都可能暂时无输出。
4. RGB565 缓冲区仅在下一次 `push` / `flush` / `reset` / `destroy` 之前有效。
   显示驱动若异步 DMA 读取它，回调须等传输完成，或将像素复制到自己的显示缓冲区。
5. 仅在流结束时调用 `flush`，用于排出重排缓冲中的 B 帧。新会话前调用 `reset`。
6. 错误处理应显示真实状态。参数/尺寸错误不能继续冒充成功输出；参考帧损坏时应
   重置、重新取得 SPS/PPS 和 IDR，再恢复显示。解码器已关闭错误隐藏模式。

## 画面比例、颜色和触摸坐标

缩放采用最近邻抽样，保留画面比例并居中，未覆盖区域写成 RGB565 黑色。对合法
VUI 中的像素长宽比也参与宽高比计算。该方案计算量较低，但文字缩小后会出现锯齿；
更好的缩放需要另行衡量 PPA、显示分辨率与额外缓冲区，当前没有冒充已经接入。

转换读取 OpenH264 给出的实际 Y/U/V 行跨度，不假设 I420 紧密连续。支持 VUI 中
的 BT.601 / BT.709 矩阵与 full / limited range；未指定矩阵时采用 BT.601。
不支持的显式色彩矩阵会被拒绝。RGB565 为数值位布局 R5:G6:B5，LCD 总线是否需要
字节交换由板级驱动决定，不能再将字节交换问题归因于 H.264 颜色解码。

触摸落在 `view_rect` 外时忽略；在矩形内时按相对位置映射到视频坐标。例如：

```text
video_x = (touch_x - view_rect.x) * source_width  / view_rect.width
video_y = (touch_y - view_rect.y) * source_height / view_rect.height
```

用实际回调的矩形计算，避免在有黑边或竖屏设备上把触点拉伸到错误位置。macOS
桌面中的逻辑坐标、HiDPI 与 USB HID 映射由上层处理，这个组件不伪装为 Sidecar
触控通道。

## 输入和内存限制

| 项目 | 当前边界 |
|---|---|
| 编码族 | AVC Baseline / Main / High；不接受 HEVC、MVC、SVC 或分区 slice |
| 像素格式 | Progressive、8-bit、4:2:0 |
| 每个维度默认上限 | 1280；配置为 0 时使用默认值 |
| 可配置的维度硬上限 | 每边 1920，可见像素总数不超过 1920×1080 |
| 编码宏块补齐 | 在所配可见尺寸基础上最多补齐至 16 像素边界，编码像素总数不超过 1920×1088 |
| 参考帧 | SPS `max_num_ref_frames <= 4` |
| 单个 AU | 默认 1 MiB，可配置硬上限 4 MiB |
| 单个参数集 | 最多 4096 字节 |
| 每 AU 的 NAL 数 | 最多 1024 |
| RGB565 屏幕缓冲 | `panel_width * panel_height * 2` 字节，另有一行横向采样索引 |

SPS 在送入 OpenH264 前先解析并检查尺寸、位深、色度格式和参考帧数量，避免仅在
解码器已经申请大块内存之后才拒绝超限视频。正常的 H.264 语法和完整 VUI 仍由
OpenH264 继续校验；这不是完整的恶意媒体安全审计。

在 ESP 目标上，输出缓冲及 OpenH264 的主要对齐工作缓冲要求
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`。PSRAM 分配失败后直接报错，不把大缓冲区
悄悄挪到内部 RAM。少量 decoder 控制对象使用检查过的 `std::nothrow` 分配。
内存需求不止一个 RGB565 屏幕：该版本单线程路径为参考图片保留约
`max_num_ref_frames + 2` 个图片槽，还包含运动矢量、系数、NAL 等工作区。以 4 个
参考帧、1080p 的 I420 像素平面粗算，六个槽的像素数据就约 18.8 MB，尚未包括
这些额外开销、网络及 LCD 缓冲。允许接受一个尺寸不等于承诺设备内存一定充足。

应优先降低发送端编码分辨率和参考帧数；只把 1080p 图像在接收端缩成小屏幕，仍需
支付完整 1080p 的熵解码、重建和参考图片成本。解码任务建议先分配至少 24 KiB
栈空间，实机再通过 stack high-water mark 校准；这是工程起始值，不是实测保证。

## 帧率与接收队列

OpenH264 当前以单线程、portable scalar C++ 构建，没有 RVV 汇编或 P4 专用加速。
AirPlay 接收端的 `maxFPS` 只表达发送期望，不是 P4 实测帧率。
此组件允许丢显示输出，仍保留压缩 P/B 帧的解码，因为后续图像可能引用它们。

默认 `FASTSYNC` 请求清晰档一半的宽高（Claw4 为 360×360），`SHARP` 请求 720×720；
两档选择保存在 NVS。收到低分辨率视频后，最近邻放大复用重复的源像素及源行，
减少 YUV 转 RGB 运算。1.5 倍和 2 倍放大的三种真实视频 profile 均通过参考采样比较。

`main.c` 的压缩队列缩短为 2 帧，字节预算同时计入正在解码的 AU。
积压时若距上次 UI 发布结束不足 150 ms，就跳过该解码输出的 RGB 转换和发布，
让解码尽快追赶；队列已经清空时立即显示，不强制把空闲场景限制为固定帧率。
队列溢出、接收端
报告丢包或解码错误时，清空剩余预测帧并重新等待 IDR；接收器会在 IDR 前附上
SPS/PPS。会话 epoch 防止旧解码回调覆盖新会话，也防止旧会话的迟到错误清掉新
队列。解码器 reset 失败会报错，不继续向失效 handle 推入视频。

设备每 30 个 AU 记录队列等待时间，每 30 个已发布输出记录实际尺寸、解码、转换
及发布耗时。`output` 与 `frame` 的差可用于观察跳过的输出。日志不记录视频像素
或加密密钥。仍需实测新版实际输出 fps、PSRAM 余量和 LCD 显示延迟。
持续积压时应降低发送参数或中止该流并重新协商；当前代码不声称会自动协商成功。
不要以丢掉预测帧后继续显示损坏画面的方式制造较高帧率数字。

设备状态界面的 `DECODED` 表示 UI 收下并复制的解码帧；`SHOWN` 只在板级显示函数
对一幅新视频帧返回成功后增加。同一帧因键盘或状态变化重绘，不重复增加 `SHOWN`。
这一区分用于排查软件管线，不能替代真实面板和 macOS 连接测试。

## 构建和复现

ESP-IDF 工程直接引用本组件，使用 IDF 自带 `pthread` 和 `heap` 依赖。解码器源代码
已随项目提供，不需要未执行的 bootstrap，也不会在构建时下载 OpenH264。

从项目根目录执行主机烟测：

```sh
cmake -S tests/decoder -B /tmp/p4-decoder-host -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/p4-decoder-host -j 4
ctest --test-dir /tmp/p4-decoder-host --output-on-failure
/tmp/p4-decoder-host/decoder_smoke
```

测试所需的小型 `.h264` 和 RGB 参考采样已检入 `tests/decoder/fixtures`。
可用安装了 libx264 的 FFmpeg 重新生成：

```sh
python3 tests/decoder/generate_fixtures.py
```

另有针对实际 `main.c` / `native_ui.c` 的主机回归测试：

```sh
sh tests/runtime/run.sh
```

该测试用 ESP/FreeRTOS 桩控制执行交错，验证队列预算、旧 epoch、reset 失败、同步
配对回调的锁顺序、长按重置取消、黑边触摸和显示计数。它编译真实应用源码和真实
输入状态机，但不模拟无线协议、RTOS 的全部调度行为或硬件时序。

`manifest.json` 记录视频 profile、B 帧信息、生成参数和 SHA-256。重生成时，FFmpeg /
libx264 版本差异可能改变压缩字节和少量 RGB 舍入结果，所以脚本同时重新生成参考
采样；测试本身不需要 FFmpeg 或网络。

## 上游与许可证

源代码固定来自
[jonasborn/openh264-esp32 @ 4a7856e61919d2c7b580413151f3cfb1323a3585](https://github.com/jonasborn/openh264-esp32/tree/4a7856e61919d2c7b580413151f3cfb1323a3585)。
只携带 decoder/common/API 所需 C++ 与头文件，未携带编码器或架构汇编。
[原始 decoder API](https://github.com/jonasborn/openh264-esp32/blob/4a7856e61919d2c7b580413151f3cfb1323a3585/codec/api/wels/codec_api.h)
和
[decoder 实现](https://github.com/jonasborn/openh264-esp32/blob/4a7856e61919d2c7b580413151f3cfb1323a3585/codec/decoder/plus/src/welsDecoderExt.cpp)
是本封装实际调用的接口来源。

本地修改、上游提交和文件范围记录在
`firmware/components/video_decoder/third_party/openh264/PROVENANCE.md`。
同目录保留上游 `LICENSE` 和 `NOTICE`；重新分发时一起保留。它们包括 BSD 源码
许可和 Cisco 对二进制分发／专利许可范围的原始说明。
