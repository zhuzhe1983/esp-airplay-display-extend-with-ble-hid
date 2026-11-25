# C5 / C6 无线协处理器固件

P4 上的 BLE 键盘/触控板依赖 C5/C6 提供蓝牙控制器。P4 主固件中的 NimBLE
是 Host，HCI 数据与 Wi-Fi 一起经过 SDIO；无线芯片上运行 **controller-only**
固件。这里提供对应的 ESP-Hosted 从机编译配置，不包含自动烧录或自动升级。

## 固定源码和配置

使用 ESP-IDF **5.5.4** 和
[espressif/esp-hosted-mcu v2.12.12](https://github.com/espressif/esp-hosted-mcu/tree/v2.12.12/slave)，
固定 Git 提交：

```text
471900d45c3b9208da10a01beb200b97f15a161a
```

构建入口是该仓库的 `slave/`，必须保留同仓库的 `common/` 等相邻目录。
不同芯片采用不同 build 目录和 sdkconfig：

| 板卡 | IDF target | 本工程 overlay | 分区配置 |
| --- | --- | --- | --- |
| MetalioClaw4 | esp32c5 | `boards/metalio_claw4.defaults` | 上游 `partitions.esp32c5.csv`，分区表 0xc000 |
| M5Stack Tab5 | esp32c6 | `boards/m5stack_tab5.defaults` | 上游 `partitions.esp32c6.csv`，分区表 0x8000 |

在本工程根目录，先载入 ESP-IDF 5.5.4 环境，再使用构建辅助脚本：

```sh
python tools/build_coprocessor.py metalio_claw4
python tools/build_coprocessor.py m5stack_tab5
```

脚本只构建，不烧录。`locks/esp32c5.lock` 和 `locks/esp32c6.lock` 保存此次
成功构建解析到的依赖版本与组件 hash；脚本按 target 预置对应锁。锁内唯一
本地组件 `cmd_system` 来自所固定的 IDF，其路径用 `${IDF_PATH}` 占位，
预置时展开为当前 IDF 安装目录。两个 target 共用一个上游 `slave` 目录时，
应依次构建，避免组件管理器同时改写该目录下的 `dependencies.lock`。

`SDKCONFIG_DEFAULTS` 必须按顺序包含：上游 `sdkconfig.defaults`、上游的
`sdkconfig.defaults.esp32c5` 或 `.esp32c6`、本工程对应 overlay。以 Tab5 为例，
下面展示底层构建参数；手动执行时也应先展开并复制对应依赖锁。把三个
绝对路径改为实际源码和输出位置后，在上游 `slave` 目录执行：

```sh
idf.py -B /absolute/build-radio-c6 \
  -D IDF_TARGET=esp32c6 \
  -D SDKCONFIG=/absolute/build-radio-c6/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=/absolute/esp-hosted-mcu/slave/sdkconfig.defaults;/absolute/esp-hosted-mcu/slave/sdkconfig.defaults.esp32c6;/absolute/p4-native-display/firmware/coprocessor/boards/m5stack_tab5.defaults' \
  build
```

Claw 使用 `esp32c5` target、对应上游 defaults 和 Claw overlay。构建产物是
`network_adapter.bin`，另有 bootloader、partition-table 和 `flasher_args.json`。
`network_adapter.bin` 是**应用镜像**，不能当作完整 Flash 镜像一律写到 0x0。
首次完整串口写入应使用当前构建生成的各分区位置，而不是复制另一芯片或
另一版本恢复固件的地址。

两份 overlay 明确启用：

```text
CONFIG_ESP_HOSTED_CP_WIFI=y
CONFIG_ESP_HOSTED_CP_BT=y
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_ONLY=y
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=y
CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y
CONFIG_ESP_SDIO_HOST_INTERFACE=y
CONFIG_ESP_SDIO_HIGH_SPEED=y
CONFIG_ESP_SDIO_STREAMING_MODE=y
```

无线芯片不运行第二套 NimBLE/Bluedroid Host，也不把 HCI 改走 UART。
首版关闭 network split、host power save 和系统 light sleep，便于先确认
传输、Wi-Fi 与 BLE 同时工作。它保留软件共存，不承诺满速投屏时的 BLE 延迟。
编译配置使用 4 MB 分区和保守的 DIO / 40 MHz Flash 设置；这并不是厂家镜像
的逐字节复现。依赖的实际版本应随各 target 的 `dependencies.lock` 保存。

## 已核实的 SDIO 两端映射

| 信号 | Claw P4 | Claw C5 | Tab5 P4 | Tab5 C6 |
| --- | --- | --- | --- | --- |
| CMD | 50 | 10 | 13 | 18 |
| CLK | 51 | 9 | 12 | 19 |
| D0 | 49 | 8 | 11 | 20 |
| D1 | 34 | 7 | 10 | 21 |
| D2 | 31 | 14 | 9 | 22 |
| D3 | 53 | 13 | 8 | 23 |
| RESET | 54 | EN | 15 | EN |

C5/C6 的 SDIO 从机信号由 IOMUX 固定，不可以把 P4 侧的自定义 GPIO 号码
复制到无线端。Overlay 中的从机 pin 值用于明确记录和验证，不能改变芯片
硬件连接。依据：

- [Claw README §7.2，固定提交](https://github.com/CloudZao/MetalioClaw4/blob/ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950/README.md)。
- [IDF 5.5.4 C5 SDIO pins](https://github.com/espressif/esp-idf/blob/v5.5.4/components/soc/esp32c5/include/soc/sdio_slave_pins.h)。
- [IDF 5.5.4 C6 SDIO pins](https://github.com/espressif/esp-idf/blob/v5.5.4/components/soc/esp32c6/include/soc/sdio_slave_pins.h)。
- [Tab5 厂家原理图，第一页](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf)。

两板主固件的初始化都会先准备无线电源并释放 EN，再由 Hosted 进行复位。
Tab5 的 GPIO15 直连 EN 的实际电平及上游 Kconfig 不一致问题见
`docs/BOARDS.md`，不能仅凭“ACTIVE_LOW”这个名字推断 GPIO 的最终电平。

## MetalioClaw4：厂家镜像与恢复口的实际边界

Claw 厂家应用在
[`settings_upgrade_tab.cc`](https://github.com/CloudZao/MetalioClaw4/blob/ca3aa3fa027ff7dad2adf0c2d03c4f24aa838950/main/display/screen/settings_screen/settings_upgrade_tab.cc)
中实现 P4 经 ESP-Hosted 对 C5 的 OTA，并指向：

```text
https://cloudzaoai.oss-rg-china-mainland.aliyuncs.com/c5_hosted_2.12.12.bin
```

此次离线检查下载到的厂家应用镜像得到：

| 项目 | 检查结果 |
| --- | --- |
| 大小 | 1,255,008 字节 |
| SHA-256 | `1488735c6ed2bbb84056b568c71ad9f926f57a5b8b101b8dbf3a059fe622983d` |
| 项目名 / App version | `network_adapter` / `2.12.12` |
| 构建 IDF | `v5.5.4-dirty` |
| 镜像头 Flash 设置 | 4 MB、DIO、80 MHz |
| 最低 C5 revision | 1.0 |

镜像含蓝牙 Feature Control、控制器初始化、VHCI 和 HCI over SDIO 相关代码
字符串，因此有静态证据表明厂家提供的这份镜像编入了 BLE/HCI 支持。
这不等于确认每一台出厂设备已经装了该镜像，也不等于已经验证 P4 NimBLE
与 macOS 的配对。应先读取实际从机版本、确认 Hosted 控制器初始化成功，
再决定是否需要变更 C5 固件。

**C5 的板上 UART0 / BOOT / EN 测试点物理位置未获厂家公开资料确认。**
仓库说明的 CH340K USB 串口属于另一颗蓝牙音频芯片，不能拿它当作 C5 下载口。
目前能核实的厂家升级路径是**已有 C5 RPC 正常工作时经 SDIO OTA**。
若 C5 不再响应 RPC，这条路径不能代替硬件恢复口；公开的
[厂商仓库恢复问题 #23](https://github.com/CloudZao/MetalioClaw4/issues/23)
也在询问该接口，检查时尚无厂商答复。

芯片本身的 ROM 下载条件是复位时 GPIO28 为低、GPIO27 为高，见
[Espressif C5 boot mode 文档](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/advanced-topics/boot-mode-selection.html)。
这里仅给芯片级条件，**不据此猜测 Claw PCB 上哪一个焊盘可以连接**。
本工程不在启动时自动 OTA 或擦写 C5；也未把未核实的物理恢复步骤写成可执行脚本。

## Tab5：已公开的 C6 下载接口

[M5Stack 官方 C6 恢复教程](https://docs.m5stack.com/en/guide/tab5/c6_wifi_restore)
说明板内 C6 默认预装 Wi-Fi SDIO 固件，并提供专用下载接口，可接官方
ESP32 Downloader / 对应 USB-TTL 下载器。它不是给 P4 烧录用的同一个逻辑串口。

官方原理图第一页的 **J1 / C6_ISP** 为：

| J1 pin | 板上信号 | 用途 |
| --- | --- | --- |
| 1 | WLAN_3.3V | 无线电源 / 电平参考 |
| 2 | RF_C6_TXD | C6 UART TX，接下载器 RX |
| 3 | RF_C6_RXD | C6 UART RX，接下载器 TX |
| 4 | RF_C6_RST | C6 EN |
| 5 | RF_C6_IO9 | C6 BOOT strap |
| 6 | GND | 公共地 |

这里的 pin 数字是**原理图 J1 编号**，物理插头的朝向应按厂商连接照片和 PCB
标记确认。信号是 3.3 V 逻辑，不能把 5 V UART 电平接到该接口；电源接法按
原厂下载器要求处理，避免重复给同一电源轨供电。

C6 在 EN 复位释放时 GPIO9 为低进入 ROM 下载，GPIO8 须为高。原理图上
GPIO8/9 已有上拉，J1 把 GPIO9 引出；见
[Espressif C6 boot mode 文档](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/advanced-topics/boot-mode-selection.html)。
P4 的 `board_init()` 会使 0x44 P0 的 WLAN_PWR_EN 为高，保证 C6 有电。
正常应用运行时 Hosted 会控制 GPIO15/EN，因此实际维护时应按厂家流程协调
P4 的 reset 控制，不能让下载器和运行中的应用争用 EN。

M5Stack UserDemo 仓库公开的恢复文件名是
[`ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin`](https://github.com/m5stack/M5Tab5-UserDemo/tree/main/platforms/tab5/wifi_c6_fw)。
它是厂家的旧版完整恢复镜像，名称已表明其版本不是本项目固定的 2.12.12。
厂家恢复页面没有承诺其中启用了本工程需要的 BLE/HCI 功能；本工程也没有
把恢复成功等同于新的 HOGP 键盘必然可用。需要匹配版本和功能时，使用这里
可构建的 2.12.12 Wi-Fi + BLE controller 配置，并先保留可恢复的原厂固件。

## 验证范围

2026-11-23，使用 ESP-IDF 5.5.4、RISC-V GCC 14.2.0 和上面的固定 Hosted
提交完成两次独立编译、链接、镜像生成及分区尺寸检查：

| Target | 应用镜像大小 | 应用写入地址 | 应用 SHA-256 |
| --- | --- | --- | --- |
| ESP32-C5 / Claw | 1,252,592 字节 | `0x20000` | `d8dcd2bd0e01406e1d1588c8d165573cf9553d25c0d185119bceb2a40e1fe162` |
| ESP32-C6 / Tab5 | 1,211,344 字节 | `0x10000` | `d04425b24dd0e97b7ac3ce3e144ad664fa87ef7da5431ccab3c189d191205dc9` |

这些 hash 标识此次生成的应用文件。重新构建可能因编译时间等元数据而产生
不同 hash；首次完整写入还包括 bootloader、分区表、OTA data，仍以当前
构建生成的 `flash_args` 为准。上表不是把应用单文件当完整恢复镜像的烧录指令。

已检查生成的 `sdkconfig`，确认 Wi-Fi、BLE controller-only、RAM HCI、软件
共存和两端 SDIO pin 均符合本文件配置。上游在 IDF 5.5.4 下会打印未使用的
ESP32 UART Kconfig 范围及 `ESP_CACHE_MALLOC` 符号等警告；没有修改固定
上游代码掩盖警告，两个 target 均完成构建。

配置来自固定版本的真实 Kconfig 和驱动源码；SDIO 两端 pin 已核对到芯片
定义与厂家连线。编译结果只证明相应源码/配置可构建。两块板都还需要
实际检查从机版本、SDIO 握手、Wi-Fi、BLE controller 初始化、macOS 配对和
高负载下的共存表现。本工程没有执行任何无线芯片烧录，也没有声称实机通过。
