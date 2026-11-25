# 0.1.0 验证记录

验证日期：2026-11-24（Asia/Shanghai）。交付为实验性源码版本。

## 完整目标构建

使用 ESP-IDF v5.5.4，固定 SDK commit `735507283d5b2f9fb363a1901172dbd9e847945d`，RISC-V GCC 14.2.0 / `esp-14.2.0_20260121`。两份 P4 最终构建已包含固定 BLE 名称、最终 UI 布局和 AP 配网接口校验修改；不是只编译单个空组件。

| 目标 | 结果 | App 镜像字节数 | 本次构建 App SHA256 |
| --- | --- | ---: | --- |
| metalio_claw4 / P4 | 完整编译、链接、镜像生成通过 | 1,674,256 | `a8c713a1fdd65977aff34ca8d5fd8c701a30570563fb9447ccadcb648833f2cf` |
| m5stack_tab5 / P4 | 完整编译、链接、镜像生成通过 | 1,697,920 | `e0f1fbd1ac3da3a1c60a196231ab6bfba0754b57eaa9026d29f07aa6fa76da42` |
| esp32c5 / Hosted Wi-Fi + BLE controller | 完整编译、链接、镜像生成通过 | 1,252,592 | `d8dcd2bd0e01406e1d1588c8d165573cf9553d25c0d185119bceb2a40e1fe162` |
| esp32c6 / Hosted Wi-Fi + BLE controller | 完整编译、链接、镜像生成通过 | 1,211,344 | `d04425b24dd0e97b7ac3ce3e144ad664fa87ef7da5431ccab3c189d191205dc9` |

上表哈希对应本次生成的 **App 构建产物**。本次下载交付是源码 ZIP，其完整包哈希在下载站的 `SHA256SUMS.txt`，不要混用。重新构建时的时间戳/环境不同可能导致二进制哈希不同。没有对任何设备执行烧录。

P4 `sdkconfig` 已核对板型、32/16 MB Flash、P4 revision 0.x/1.x 配置、360 MHz CPU、200 MHz PSRAM、正确的 SDIO 引脚、NimBLE Host-only 和 Hosted VHCI。C5/C6 `sdkconfig` 已核对 Wi-Fi + BLE controller-only、RAM HCI、共存和固定 SDIO 从机引脚。Claw NV3051F 为完整构建的默认面板；FL7707N 分支的三份驱动源文件以实际 RISC-V 编译参数独立编译通过，尚无 FL7707N 完整镜像或面板实测结论。

## 主机软件测试

| 项目 | 实际执行 | 结果 |
| --- | --- | --- |
| H.264 Baseline | 8 个真实输出帧、8 个不同画面 | 通过；采样 RGB MAE 1.356 / 255 |
| H.264 Main | CABAC、B 帧、每 AU 两个 Slice，8 帧 | 通过；MAE 1.429 / 255 |
| H.264 High | CABAC、B 帧、每 AU 两个 Slice，8 帧 | 通过；MAE 1.753 / 255 |
| 解码边界 | SPS/PPS、截断、分辨率/参考帧限制、EOS、reset、黑边、竖屏 | 通过 |
| AirPlay 数据解析 | AVCC/avcC、plist 往返、溢出、20,000 个确定性畸形输入 | ASan + UBSan 通过 |
| PlayFair 源码 | 全部 C 源文件 `-Wall -Wextra -Werror` 主机语法检查 | 通过 |
| 输入 | 真实 HID descriptor、报文长度/LED、ASCII/组合键、松键、点击/拖动/滚动 | ASan + UBSan 通过 |
| 输入拥堵 | 64 包队列溢出、发送失败、release 恢复、过期连接代次 | 通过 |
| 运行时集成 | 实际 main/UI 源码，RTOS/设备接口使用 host stub | 三组回归通过 |
| UI 主机渲染 | 实际 draw() 的两板 home/keyboard/info/error 布局 | 8 幅图生成；检查键盘、最拥挤状态页与 Tab5 布局 |

运行时回归明确覆盖同步 PAIR 回调不在 UI 锁内、空闲不重复绘制、连续按住两秒才重置、黑边输入过滤、队列预算包含 in-flight AU、IDR 恢复、旧 epoch 不发布/清空新会话、decoder reset 失败恢复。`SHOWN` 只有新视频帧且板级呈现函数返回成功才增加，重复 UI 重绘和显示失败不计入。

UI 测试图是主机执行实际绘制代码得到的结果，**不是实机照片、不是 Mac 投屏截图**。软件测试不是无线或屏幕硬件的替代。运行时 stub 只位于 tests/ 中，生产固件使用真实 ESP-IDF 与板级驱动。

此执行环境的 ptrace 管理与 LeakSanitizer 不兼容，ASan/UBSan 测试使用 `ASAN_OPTIONS=detect_leaks=0`；地址/未定义行为检查保留，**没有声称完成泄漏检查**。解码器另有 AddressSanitizer 验证，复现细节见 VIDEO.md。

## 可复现命令

```sh
# 已 export ESP-IDF 5.5.4
python tools/build.py metalio_claw4 build
python tools/build.py m5stack_tab5 build
python tools/build_coprocessor.py metalio_claw4
python tools/build_coprocessor.py m5stack_tab5

# 普通 Linux/macOS 主机；不需要 ESP 板
python tools/test_host.py
# 仅受 ptrace 管理而不支持 LSan 的环境使用：
ASAN_OPTIONS=detect_leaks=0 python tools/test_host.py
# 实际固件 UI 的 PPM 布局输出
sh tests/runtime/run.sh --render /tmp/p4-native-ui
```

Coprocessor 的实际构建在固定上游 `slave/` 进行，脚本使用同样的 target、defaults 顺序和 SDKCONFIG 参数。两份成功解析的依赖锁保存在 `firmware/coprocessor/locks`；脚本会展开 SDK 自带 cmd_system 的本机路径，不携带验证环境的绝对路径。

## 未验证与未实现

- 没有真实 Mac、Claw4 或 Tab5 硬件接入本次执行环境。
- 未验证 macOS 的实际发现/握手/持续视频、镜像与扩展、蓝牙配对、中文输入、面板颜色/触控精度、无线共存和断线恢复。
- 未测 P4 帧率、端到端延迟、最高可用分辨率、温度和峰值内存。640×360 / 10 fps 是请求参数，不是测试结果。
- 未实现完整 Apple Sidecar 接收端。扩展显示器的绝对 HID 定位未验证，默认关闭实验模式。
- 上游 ESP32 接收器存在 Mac 完成握手却无视频的 issue #46。时序/密钥条件修正已写入，但没有实机证据证明该问题已经解决。

下一阶段按 [ACCEPTANCE.md](ACCEPTANCE.md) 记录真实设备结果。不能根据“编译通过”在上面这些项目上填“通过”。
