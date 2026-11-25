# 板级适配与硬件边界

本工程的显示层目标是 **MetalioClaw4** 和 **M5Stack Tab5**。两者均使用
ESP32-P4 运行应用、外接 C5/C6 运行无线固件、MIPI-DSI 输出到内置屏幕。
这里实现的是实际的屏幕初始化、触控读取、背光和无线芯片上电接口。
**代码依据上游源码和厂家资料编写，尚未在这两块实物上烧录验证。**
能编译、能初始化屏幕和能完成 macOS AirPlay 投屏是不同的验证项目。

## 选择设备

| 项目 | MetalioClaw4 | M5Stack Tab5 |
| --- | --- | --- |
| 构建参数 | `-D BOARD=metalio_claw4` | `-D BOARD=m5stack_tab5` |
| 默认屏幕 | NV3051F，720 × 720 | BSP 自动识别，720 × 1280 |
| 其他屏幕 | FL7707N，通过 menuconfig 选择 | ILI9881C + GT911、ST7123、ST7121 |
| 无线协处理器 | ESP32-C5 | ESP32-C6 |
| Flash 默认值 | 32 MB，来自 Claw 上游 sdkconfig | 16 MB，来自 M5Stack 产品资料 |
| CPU 默认频率 | 360 MHz | 360 MHz |
| 显示扫描缓冲 | 2 × 720 × 720 × 3 = 3,110,400 字节 | 2 × 720 × 1280 × 2 = 3,686,400 字节 |
| 源图像接口 | 小端/主机字节序 RGB565 | 小端/主机字节序 RGB565 |
| 面板输出格式 | 保留上游 RGB888 配置 | 官方 BSP 的 RGB565 配置 |

方向固定为面板的物理原生方向。Tab5 的产品宣传常写 1280 × 720，但底层
DSI 面板和触控坐标为 **720 × 1280**；本版不在板级偷偷交换横纵坐标。
视频解码/缩放组件应依据 `board_info()` 返回的尺寸生成整帧。旋转若后续加入，
需要同时改变图像和触控映射。

在 ESP-IDF **5.5.4** 环境中，从 `firmware` 目录构建：

```sh
idf.py -B build-claw4 -D BOARD=metalio_claw4 build
idf.py -B build-tab5 -D BOARD=m5stack_tab5 build
```

两个构建目录各自持有 sdkconfig，避免不同板子的引脚和 Flash 配置交叉复用。
首次配置从 `sdkconfig.defaults` 和对应 `boards/*.defaults` 读取；已有 sdkconfig
不会因为改动 defaults 自动覆盖用户设置。

这两份默认配置明确选择 **P4 revision 0.x/1.x**：
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`、`CONFIG_ESP32P4_REV_MIN_0=y`。
Claw 上游就是这一配置，Tab5 的已知 360 MHz P4 也属此前版本。
ESP-IDF 5.5.4 本身默认选择 rev3.1，不能原样用于旧 P4。
IDF 明确规定 `<3.0` 和 `>=3.0` 两类硬件的固件支持互斥；如果实际设备更换
为 rev3.x 芯片，应根据串口启动日志/芯片信息单独生成相应构建，不能把这里
生成的旧版二进制直接视为通用版本。依据见
[IDF 5.5.4 P4 revision Kconfig](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_hw_support/port/esp32p4/Kconfig.hw_support)。

Claw 的 FL7707N 模块使用：

```sh
idf.py -B build-claw4 -D BOARD=metalio_claw4 menuconfig
```

在 **Display board → MetalioClaw4 LCD module** 选择 FL7707N。
本工程不会通过模糊探测替用户决定 Claw 面板型号。

## 软件依赖与来源

| 依赖 | 版本要求 / 来源 | 用途 |
| --- | --- | --- |
| ESP-IDF | 5.5.4，项目约束 `<5.6` | P4、DSI、I2C、LEDC、PSRAM |
| `espressif/m5stack_tab5_noglib` | 固定 1.3.3 | Tab5 官方 BSP，无 LVGL |
| `espressif/esp_io_expander_tca95xx_16bit` | `^2.0.0` | Claw 的 TCA9555 |
| `espressif/esp_lcd_touch_gt911` | `^1.0.0` | Claw 的 GT911、Tab5 旧版触控 |
| `espressif/esp_lcd_touch` | `^1.2.1` | 公共触控接口，使用 `esp_lcd_touch_get_data()` |
| `espressif/esp_hosted` | 固定 2.12.12 | P4 与 C5/C6 的 SDIO 和蓝牙 HCI 通路 |
| `espressif/esp_wifi_remote` | 固定 1.6.4 | P4 上的远程 Wi-Fi API |

组件管理器生成的 `dependencies.lock` 是一次构建实际解析到的版本记录。
Tab5 BSP 自带的音频、相机、IMU、USB 依赖仍由组件管理器解析；本显示层不会
启动这些外设，也不会启动 LVGL。

Claw 面板完整寄存器初始化表来自
[CloudZao/MetalioClaw4 的固定提交 ca3aa3f](https://github.com/CloudZao/MetalioClaw4/tree/ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950/main/boards/metalio-claw-4)。
四份驱动文件及上游 MIT 许可证保存在
`firmware/components/display_board/vendor/metalio_claw4`，来源见该目录的
`PROVENANCE.md`。

Tab5 的已发布包可查
[Component Registry：m5stack_tab5_noglib 1.3.3](https://components.espressif.com/components/espressif/m5stack_tab5_noglib/versions/1.3.3/readme)。
本版已检查发布包中的源码，ST7121 支持确实在该版本中，不依赖未发布的
GitHub master 修改。

## MetalioClaw4

### 显示和触控

| 信号 | 配置 |
| --- | --- |
| I2C | 控制器 1；SDA GPIO7、SCL GPIO8 |
| LCD 复位 | GPIO3，低电平复位 |
| LCD 背光 | GPIO52，LEDC 5 kHz、10 位、非反相 |
| DSI PHY 供电 | 内部 LDO 通道 3，2500 mV |
| DSI 数据 | 2 lanes，每 lane 1000 Mbps |
| DPI pixel clock | 48 MHz，来自当前上游实际代码 |
| GT911 | I2C 0x5D / 0x14 依次探测，200 kHz；INT GPIO33 |
| 坐标 | 720 × 720，无镜像、无交换 |

NV3051F 时序为 HBP/HSW/HFP = 44/2/46，VBP/VSW/VFP = 14/2/16。
FL7707N 时序为 120/60/106 和 20/4/20。保留上游面板和 DSI 的 RGB888 设置，
其中 FL7707N 的 `panel_config.bits_per_pixel=16` 与 RGB888 DSI 配置组合也
保留原样；这是上游代码的实际组合，仍须以实物色条检查确认。

`board_present_rgb565()` 将 RGB565 扩展到 IDF RGB888 内存顺序 B、G、R。
它因此会产生每帧一次格式转换成本。此处优先保留上游面板配置；没有把这种
实现写成经过实测的帧率保证。

### TCA9555 电源和 IO

TCA9555 地址为 0x20，映射来自上游 `main/boards/common/IOExpander.hpp`。

| IO 位 | 功能 | 本固件初始化 |
| --- | --- | --- |
| P0 | GPS_POWER | 低 |
| P1 | PA_SWITCH，音频 4G/Wi-Fi 路由选择 | 高，与上游 Wi-Fi 路由一致 |
| P2 | CAM_PWDN，摄像头低有效供电 | 高，关闭摄像头 |
| P3 | SD，低有效供电 | 高，关闭未使用的 SD 卡 |
| P4 | PWR_KEY_PULSE | 低，不发开关机脉冲 |
| P5 | PWR_KEY | 输入 |
| P6 | 独立蓝牙音频芯片 BT_POWER | 低 |
| P7 | RST_4G | 高，与上游初始值一致 |
| P8 | 音频功放 PA | 低 |
| P9 / P10 / P11 | 加速度中断 / USB 插入 / 无线充电检测 | 输入 |
| P12 | USB_OTG_SW，USB VBUS 输出开关 | 低 |

初始化先通过 TCA 驱动的输出寄存器操作写好锁存值，再设置输出方向，避免
默认高电平在配置方向时触发开关机脉冲或 USB 输出电源。其余引脚保持输入。
不执行上游应用中与此任务无关的充电 IC 定制写入。

**P6 的 BT_POWER 控制的是独立音频芯片，并不是 ESP32-C5 的 BLE 电源。**
C5 使用板上供电、GPIO54 复位；`board_init()` 将该脚释放为高电平，
随后由 ESP-Hosted 执行复位脉冲及 1500 ms 等待。
上游提到 `InitializeSDWIFIPower()` 的注释没有对应实现，不能据此猜测还要
为 slot 1 打开 SD 卡 LDO4。本工程使用上游 SDIO slot 1 外部供电配置。

GPIO3 也会被上游相机逻辑使用，因此后续增加相机时不能独立重新拉低该引脚，
否则显示寄存器可能丢失。GPIO53 已用于 SDIO D3，本工程不会将它同时配置成
上游另一处定义的 `USB_CHG_STA_PIN` 输入。

## M5Stack Tab5

### BSP 负责的硬件差异

| 硬件版本 | 触控识别 | 显示配置 |
| --- | --- | --- |
| ILI9881C + GT911 | GT911 地址 0x14 | BSP ILI9881C 初始化表 |
| ST7123 | 地址 0x55，固件版本寄存器 0x0000 值为 3 | BSP ST7123 初始化表 |
| ST7121 | 地址 0x55，固件版本值为 1 | BSP ST7121 初始化表；lane rate 965 Mbps |

这些判断和面板命令由官方 BSP 1.3.3 实现。本适配在调用 `bsp_display_new()`
之前补齐实际复位、有限次数的 I2C 启动探测和 ST7121 的 965 Mbps 选择。
原因是该版本 BSP 把 965 Mbps 选择放在 LVGL 包装函数中，直接使用无图形库
接口时需要传入相同参数。未知固件版本返回错误，不套用任意已有时序。

| 信号 | 配置 |
| --- | --- |
| I2C | SDA GPIO31、SCL GPIO32，控制器 1 |
| 第一 IO 扩展器 | PI4IOE5V6408，0x43 |
| 第二 IO 扩展器 | PI4IOE5V6408，0x44 |
| LCD 复位 | 0x43 P4，通过 BSP 控制 |
| 触控复位 | 0x43 P5，通过 BSP 控制 |
| 触控中断 | GPIO23；旧版 GT911 所需的引脚处理由 BSP 完成 |
| 背光 | GPIO22，通过 BSP LEDC 接口 |
| DSI PHY | LDO3 2500 mV，由 BSP 申请 |
| C6 电源 | 0x44 P0，`bsp_feature_enable(BSP_FEATURE_WIFI, true)` |
| 天线选择 | 0x43 P0 低电平，使用内置天线 |
| C6 复位 | GPIO15，空闲高电平、复位脉冲低电平 |

ST7123/ST7121 的 LCD_RST 拉低时使用输出，释放时必须切为带上拉的输入；
BSP 1.3.3 的 `bsp_feature_enable()` 已实现这一处理。本代码调用该接口，
没有把高电平释放改成普通推挽输出。初始化会先真正拉低 LCD/触控复位，再释放。

M5Stack 官方资料记录了 2025-10-14 从 ILI9881C/GT911 改为 ST7123、
2026-04-28 改为 ST7121；可查看设备背部标签确认实际模块。
引脚、无线电源、天线和屏幕复位要求均见
[M5Stack Tab5 官方文档](https://docs.m5stack.com/en/core/Tab5)。

## ESP-Hosted：两块板不可混用的引脚

两块板都使用 SDMMC **slot 1、4 bit、40 MHz**，Wi-Fi 与 BLE HCI 共用此链路。
对应的 C5/C6 必须运行兼容 ESP-Hosted 的 **Wi-Fi + BLE** 从机固件。
仅在 P4 打开 NimBLE 开关不会自动把已有 Wi-Fi-only 从机变成 BLE 控制器。

| 信号 | MetalioClaw4 / C5 | Tab5 / C6 |
| --- | --- | --- |
| CMD | GPIO50 | GPIO13 |
| CLK | GPIO51 | GPIO12 |
| D0 | GPIO49 | GPIO11 |
| D1 | GPIO34 | GPIO10 |
| D2 | GPIO31 | GPIO9 |
| D3 | GPIO53 | GPIO8 |
| RESET | GPIO54 | GPIO15 |
| 复位空闲电平 | 高 | 高 |
| Hosted 配置名字 | `ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH` | `ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH` |

这里 ESP-Hosted 的 `ACTIVE_HIGH/LOW` 名称指其配置约定；按上表的实际空闲
电平和脉冲理解最清楚。P4 应先调用 `board_init()`，再启动 ESP-Hosted/
远程 Wi-Fi/远程 BLE。板级启动只释放 reset 和准备供电；由 ESP-Hosted
管理实际从机复位过程，避免两个模块交替操作同一复位脚。

**Tab5 的上游配置有一个需特别核对的矛盾：** Hosted 2.12.12 的 Kconfig
为 Tab5 默认选择 `ACTIVE_LOW`，M5Stack 的旧版 UserDemo 也保存过这个值。
但是该版本 `sdio_drv.c` 的复位序列是 ACTIVE → INACTIVE → ACTIVE，
选 LOW 会最终保持 GPIO15 为低。当前厂家原理图第一页清楚显示
P4 GPIO15 经 R4 1 kΩ 直连 C6 EN，R6 10 kΩ 上拉，没有反相级；EN 必须为高
才能运行。因此本工程显式覆盖为 `ACTIVE_HIGH`，并在 board_init 中先拉高。
这是基于原理图和所固定版本驱动代码作出的实现选择，仍需在实际板上测量
GPIO15/EN 电平确认。原理图见
[Tab5 Schematics PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf)，
复位代码见
[Hosted 2.12.12 SDIO driver](https://github.com/espressif/esp-hosted-mcu/blob/v2.12.12/host/drivers/transport/sdio/sdio_drv.c)。

Claw 配置依据
[上游 sdkconfig](https://github.com/CloudZao/MetalioClaw4/blob/ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950/sdkconfig)；
Tab5 引脚和复位选择同时对照
[ESP-Hosted v2.12.12 Kconfig](https://github.com/espressif/esp-hosted-mcu/blob/v2.12.12/Kconfig)
及 M5Stack 板卡资料。

## API、缓冲所有权与现场验证

`display_board.h` 是应用与板级代码的唯一公共接口：

```c
esp_err_t board_init(void);
const board_info_t *board_info(void);
esp_err_t board_present_rgb565(const uint16_t *pixels, uint16_t width,
                              uint16_t height, size_t stride_bytes);
int board_read_touch(board_touch_point_t *points, size_t max_points);
esp_err_t board_set_backlight(uint8_t percent);
```

`board_info()` 在初始化前即可调用。`board_init()` 应由启动任务调用一次，
失败后重新启动设备；这版没有实现复杂的失败后外设资源拆卸和重试。
渲染调用互斥；触控读取另有互斥锁，可以与帧显示并行。

`board_present_rgb565()` 只接受整幅原生尺寸图像，stride 单位为**字节**，
源数据和每行起点须满足 16 位对齐。它先复制/转换到板级双缓冲中的非显示
缓冲，再调用 IDF 的 `esp_lcd_panel_draw_bitmap()` 切换缓冲及写回缓存，
最后等待切换后的两次刷新事件再允许旧缓冲复用。这个保守等待会限制最大
提交频率，尚未按吞吐率调优。源数据在函数返回后即可复用；250 ms 刷新超时
时返回错误并停用后续呈现，避免在扫描状态不明确时写入正在显示的缓冲。

触控返回 0 表示释放，负值表示总线/控制器错误，正值是最多 5 个触点。
`id` 仅表示本次采样中的顺序，**不是跨帧稳定的多点跟踪 ID**。
坐标与显示的原生方向相同。触控到 macOS 鼠标/键盘的映射在输入模块完成。

实物首轮验证应依次确认：黑屏初始化和背光、红绿蓝/四角图案、四角触控
坐标、持续换帧无 DMA 欠载、C5/C6 启动及 Wi-Fi、BLE 配对和输入，然后才
验证真实 macOS 投屏。编译成功不能替代面板时序、颜色、触摸方向或无线
共存的实物验证。帧率、端到端延迟、功耗和多显示器绝对坐标映射仍须实测。

本板级实现没有启用 USB HID。Claw 的 USB Serial/JTAG 与其上游 USB OTG FS
用法共享 GPIO24/25；Tab5 的 USB-A 主要是 Host 接口，USB-C 提供下载和供电。
目前 Mac 输入回传采用独立 BLE HID 通路；它不是 Sidecar 的私有触控协议。

## 此次编译验证

2026-11-23 已使用 ESP-IDF 5.5.4 和 RISC-V GCC 14.2.0 完成 Claw 的 NV3051F
默认配置及 Tab5 配置的完整 P4 工程编译、链接、镜像生成。生成的 sdkconfig
已逐项核查板型、P4 revision 范围、CPU/PSRAM 频率、Flash 容量、SDIO 两端
选择、reset 电平，以及 P4 NimBLE 与远程 HCI 开关，未被其他板的默认值覆盖。
Claw 的 FL7707N 条件分支及原厂 FL 驱动也已用相同 P4 编译参数单独编译通过；
该项只属于分支编译检查，没有宣称已生成或运行 FL7707N 的整套固件。

对应的 C5、C6 Wi-Fi + BLE controller 固件也分别完成全编译；源码版本、
依赖锁、pin 映射、镜像大小与恢复接口边界见
[无线协处理器说明](../firmware/coprocessor/README.md)。
