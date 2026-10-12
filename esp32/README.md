# ESP32 最小移植

从 `esp32s31` 复制 Kernel/Lua runtime 的最小 ESP-IDF 适配：
标准堆分配/释放、`esp_timer` 单调时钟、诊断日志和单个 FreeRTOS owner 循环。
每 10 ms 调用 Kernel poll；创建 runtime，但暂不加载 Lua APP。
屏幕、音频、存储、按键、Wi-Fi/蓝牙等产品设备尚未接入。

## 默认硬件配置

面向经典 ESP32（例如 ESP32-WROOM-32），不是 ESP32-S3/C3/S31。
默认 Flash 4 MB、DIO、40 MHz，单 APP 分区；不要求 PSRAM。
串口采用 SDK 默认 UART0：TX=GPIO1、RX=GPIO3，替换原 S31 的 GPIO58/59。
`main` 栈沿用 16 KiB。具体模块的 Flash、PSRAM、串口和分区可通过 menuconfig 调整。
产品根 JSON 中未实现的 components 必须全部为 false，屏幕尺寸字段仅预留。

## 产品入口

```sh
cd ~/github/q2
source ~/github/esp-idf/export.sh
python q2.py config validate --profile esp32
python q2.py build --profile esp32
python q2.py menuconfig --profile esp32
python q2.py run --profile esp32 --port /dev/ttyUSB0
```

根配置为 `q2.esp32.json`，Linux 输出在 `build/linux/esp32`，Windows 在 `build/esp32`。
`run` 烧录后打开串口，成功初始化时输出 `Q2-Kernel port: ok`；
当前不会显示播放器主页或提供完整 MP3 功能。

## 独立 Platform 构建

不需要父仓库配置或 APP 仓库；明确指定独立 Kernel 路径：

```sh
source /path/to/esp-idf/export.sh
idf.py -C /path/to/platform/esp32 -B /path/to/build/esp32 \
  -D Q2_KERNEL_DIR=/path/to/kernel build
```

独立构建的 Q2 日志由此平台 Kconfig 配置；产品集成时以所选根 JSON 为准。
编译成功仅验证交叉构建，串口、内存余量与长期运行还需实机确认。

## 本次验证

- SDK：ESP-IDF `v6.1-rc1`，Linux/WSL，SDK 环境的 Python。
- 产品入口：`python q2.py build --profile esp32` 成功；固件 183,264 字节。
- 独立入口：上述独立 `idf.py` 命令成功；固件 183,264 字节。
- 产品 Python 回归：38 项通过；Kernel 契约检查通过。
- 尚未烧录实机，串口输出、运行堆余量和长期稳定性待确认。
