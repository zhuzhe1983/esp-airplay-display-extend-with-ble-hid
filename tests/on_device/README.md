# P4 蓝牙加密诊断

这是显式开启的实物测试，不参与普通固件构建。它调用固定 ESP-IDF 5.5.4
中 NimBLE 的内部符号，验证公开的 Bluetooth LE Secure Connections P-256
样例 DHKey，并测试随机密钥生成；配对时仅记录相应函数及控制器 RNG 的返回码。
不输出实际私钥、DHKey 或配对数据。

```sh
cd firmware
idf.py -B build-metalio_claw4 -D BOARD=metalio_claw4 -D P4_BLE_CRYPTO_PROBE=ON build
```

这只构建应用。上板应先核实设备身份、完整备份及真实分区表，再使用正确的
应用分区地址，不能将上述构建目录的默认 Flash 布局直接写到已有厂商固件的设备。

观察启动日志中的 `SC known-vector: rc=0 match=1` 和
`SC random key generation: rc=0`；前者检查结果而非只检查调用是否成功。
这两个测试通过仍不代表 Mac 配对或 HID 报告订阅成功，需要另行验证。

完成诊断后，用相同构建命令将 `P4_BLE_CRYPTO_PROBE` 设为 `OFF`；
CMake 会记住上次选择，新的构建目录默认不启用。

2026-11-25 的 P4 rev 1.0 + C5 实物上，SDK 默认 TinyCrypt 的这两个测试均
返回 17（`BLE_HS_EUNKNOWN`）。SDK 中的相关路径与
[Espressif P4 ECC 问题记录](https://github.com/espressif/esp-hosted-mcu/issues/241)
一致，但该 issue 的另一块板表现为错误 DHKey，本机则直接返回失败；
不能把两者的错误码混为一谈。

同一块板上的后续对照：mbedTLS + 硬件 ECC 调用返回 0，但样例结果不匹配；
mbedTLS + 软件 ECC 返回 `rc=0 match=1`，随机密钥生成也返回 0。
样例的期望值还通过 Mac 上独立的 Python cryptography 软件实现核对。
Claw4 的默认配置因此同时选择 mbedTLS、关闭硬件 ECC；不关闭 Secure Connections。
