# 键盘与触控输入

本工程通过 ESP32-P4 的 NimBLE host、ESP-Hosted SDIO、C5/C6 BLE controller 实现标准 HID over GATT。Mac 使用系统蓝牙支持，不运行配套客户端。此输入通路独立于 AirPlay，更不等于 Apple Sidecar 的专用输入协议。

## 报告

| Report ID | 内容 | 字节数 | 用途 |
| --- | --- | --- | --- |
| 1 | 修饰键、保留字节、六键数组 | 8 | ANSI/US 键位、Mac 修饰键 |
| 2 | 三按键、相对 X/Y、滚轮 | 4 | 默认触控板模式 |
| 3 | 三按键、16-bit 绝对 X/Y | 5 | 实验映射，范围 0…32767 |
| 4 | Consumer usage | 2 | 协议支持的媒体键，当前 UI 未提供入口 |

键盘输出报告接收 Num/Caps/Scroll 等 LED 状态；没有伪造 Apple VID/PID。报告以加密 BLE 连接发送，状态页需要看到键盘与鼠标通知已启用后才显示 `INPUT READY`。

首次 pairing 使用标准 Just Works 加密与 bonding；三分钟物理配对窗口从开机或点 `PAIR` 开始。窗口结束后，若需要重新连接，再点 `PAIR`。NVS 保留 bond。若 Mac 已缓存旧固件的不同 HID descriptor，请在系统蓝牙设置里“忽略此设备”，重新点 `PAIR` 并配对。重复配对只在打开的窗口内替换旧 bond。

UI/USB 发出的配对请求排入 NimBLE host 事件队列，避免与启动回调同时设置广播。
日志分别记录连接、加密状态、通知订阅和断开原因；只有链路加密且键盘、鼠标均订阅后才就绪。
本机 P4 rev 1.0 的 TinyCrypt 密钥生成返回错误 17；mbedTLS 硬件 ECC 的公开样例结果也不匹配。
Claw4 因此使用 mbedTLS 软件 ECC，保留 Secure Connections、bonding 和加密报告；
可用 [实物加密探针](../tests/on_device/README.md) 检查数学结果，再验证真实 Mac 配对。

## 软键盘操作

QWERTY 布局提供字母、数字、基本标点、Escape、Tab、Delete、Return、Space、方向键与四个 Mac 修饰键。点 `SHIFT`、`CTRL`、`OPT`、`CMD` 选择修饰键后点普通键；普通键释放时发送全零键盘报告，并清除选中的修饰键，避免残留按下状态。多修饰键可同时选中。

- `CMD` 对应 USB HID 的 left GUI modifier，即 Mac Command。
- `OPT` 对应 left Alt，即 Mac Option。
- `COPY` / `PASTE` 分别发送 Cmd+C / Cmd+V。
- `IME` 发送 Ctrl+Space；Mac 可以自行更改此快捷键，因此以系统设置为准。
- 长按 Delete 或方向键有重复输入；键离开按键范围或松开即释放。

中文输入示例：Mac 启用简体拼音输入源，在目标应用文本框获得焦点，点 `IME` 切换到该输入源，用软键盘输入 `nihao`，用数字键或 Space 选择候选词。设备发送物理键位，不包含一套本地中文输入法，也不通过 Mac 辅助程序直接注入 Unicode。其他键盘布局请调整 HID usage 到字符的映射，不能假设所有 Mac 都是 US 布局。

## 默认触控板模式

单指移动改变指针相对位置；轻点发送左键；原地按住约 450 ms 后进入拖动；双指移动发送滚轮；双指轻点发送右键。改变手指数时重新建立移动基准，避免坐标跳变。触控中断/读取错误会取消手势并释放按键。队列溢出时优先恢复零按键状态，防止 Shift/Cmd 或鼠标按键卡住。

使用扩展桌面时，可以像普通触控板一样把光标移动到任意显示器。**手指点触屏上的某个位置，不代表 Mac 会自动把光标放在扩展屏同一点。** 这正是普通相对鼠标和 Sidecar 绝对触控的能力差别。

轻点使用每轴 12 像素的抖动容差，450 ms 内抬起仍可点击；持续按住约 450 ms 开始拖动。
2026-11-25 的 Mac 实测已收到 Claw4 的左键按下和抬起，键盘及移动也由用户确认。
为方便将光标一次移到当前副屏中央，可在 Mac 执行：

```sh
swift tools/focus_display.swift
```

此工具通过本机实际显示器边界定位，不点击、不监听输入、不常驻。默认 serial 来自本机的
AirPlay 显示 UUID；其他显示身份可用 `--serial NUMBER` 指定。之后仍按普通触控板跨屏移动。
它完成一次定位，副屏绑定的绝对触控仍需独立实现和验证。

## 实验性绝对坐标

`menuconfig → P4 Native Display → Expose experimental absolute pointer mode` 默认关闭。开启后工具栏允许切换 `ABS TEST`，按解码器的实际 `view_rect` 忽略黑边并把图像内坐标转换成 0…32767 的绝对鼠标报告。

macOS 决定该绝对 HID 映射到哪块屏幕；本项目没有使用 Mac helper 调用显示器布局 API，也没有实现可证明固定到 AirPlay 副屏的机制。请首先在单屏或镜像模式测四角与中心，在改变缩放、旋转、主显示器或排列后重新验证。多显示器扩展模式下必须记录实际行为，不能把数学坐标转换测试当成系统映射验证。

## 真实验收

在 TextEdit 输入大小写、符号、组合键和中文，检查按键释放；在 Finder 连续拖动、滚动、切换显示器；关闭和重开蓝牙；中断触控或让队列过载，检查没有粘键。测试 Mac 与每块板子的具体系统/固件版本。测试时不要在系统密码框里录入日志或截图。
