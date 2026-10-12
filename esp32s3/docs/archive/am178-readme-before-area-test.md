> Historical snapshot; settings below are not the current defaults.

# ESP32-S3 / AM178 平台

本目录现在是真正的 ESP32-S3 构建目标，接入 Q2-Kernel、Lua API 2、LVGL 中文字体、
CO5300 QSPI 显示和 CHSC6417 单点触摸。启动时运行内嵌 display-demo，全屏纯红→纯绿→纯蓝，每 2 秒自动切换并循环。
没有按钮和文字，颜色切换不依赖触摸；用于复现显示黑屏。
目前未移植工厂 APP 管理、Flash 私有存储、Wi-Fi/HTTP、音频、蓝牙、USB 或物理按键；
这些能力不会注册成可用。PC Music APP 不能仅靠此次移植直接在设备上联网播放。

## 硬件与配置

参考配置为 **ESP32-S3 N16R8：16 MiB Flash / 8 MiB 八线 PSRAM**，这是待确认的构建假设。
默认原生竖屏 368×448；完整产品可在根 `q2.esp32s3.json` 改为 448×368 软件横屏。
面板固定 AM178Q368448LK，原生地址 X 偏移 16、Y 偏移 0。
QSPI 10 MHz（按用户本地 OSPTEK 工程），RGB565 高字节先发；Kernel 输入仍是 32 位 RGB888。

| 信号 | GPIO |
|---|---:|
| TP_RST | 9 |
| TP_INT | 46 |
| TP_SDA | 3 |
| TP_SCL | 8 |
| LCD_TE | 18 |
| LCD_RST | 17 |
| LCD_CS | 16 |
| LCD_IO3 | 15 |
| LCD_IO2 | 7 |
| LCD_IO1 | 6 |
| LCD_IO0 | 5 |
| LCD_CLK | 4 |

引脚集中在 `main/board.h`。TP_INT / LCD_TE 只配置为输入，不输出电平。
TP_INT（GPIO46）配置为下降沿中断，ISR 只向唤醒队列投递请求。
优先级 5、4 KiB 栈的独立 `q2_touch` 任务收到请求后立即读取 I²C 地址 0x2E、
寄存器 0xE0 的三字节报告；采集不再等待主循环完成同步 QSPI 刷新。
I²C 保持 100 kHz、组合事务及 ACK 检查。IO/NACK 或 timeout 最多读取三次，每次至少间隔 5 ms，
等待期间返回缓存状态，不阻塞等待也不伪造松手。每轮最多一次 I²C 读取；
没有新中断和待重试任务时不访问 I²C。重试期间的新 IRQ 合并，但不会重置次数上限。
初始化时若 INT 已为低电平，会补投一次请求；松手状态等待 TP 的报告更新。
触摸复位低电平至少 200 ms，释放后等待 200 ms；LCD 在 QSPI IO 建立后复位，低电平 10 ms、释放后等待 150 ms。
I²C 重试耗尽才释放指针并返回错误，避免持续失联时留下永久按下状态；触摸未找到时显示仍可工作。当前没有使用 TE 做防撕裂同步。

需要实际核对：

- GPIO3/46 为启动绑带引脚，GPIO46 仅输入；外设和上拉不能破坏启动所需电平。
- 原项目屏幕规格书 `AM178Q368448LK.pdf` 指定显示 IOVCC 为约 1.8 V，触摸电源为 3.3 V。
  若使用裸屏，必须确认供电和信号电平转换；不能把 ESP32 的 3.3 V 直接当作裸屏允许电平。
- 当前触摸总线启用了 ESP32 内部弱上拉（保留现有板级设置）；仍需模块侧合适的外部上拉，核对实际 IO 电压。GPIO 表没有电源使能，屏模块须自行满足 VCI_EN。
- 本配置保留 UART0 默认 TX43/RX44 输出启动日志；没有沿用 S31 的 GPIO58/59。

## 构建

完整产品在激活 ESP-IDF 环境后，于 **q2 根目录** 执行：

```powershell
.\build.ps1 -Target esp32s3
# 或使用激活环境的 Python
python q2.py build --target esp32s3
```

入口自动选择根 `q2.esp32s3.json`，不修改 PC 的 q2.json。
首次构建从本目录 sdkconfig.defaults 生成根 build/esp32s3/sdkconfig；旧目录中的 sdkconfig 不参与。
已生成 SDK 配置的内存型号等设置应通过对应构建目录 menuconfig 修改，defaults 不会覆盖已有值。

独立 Platform checkout：

```powershell
idf.py -C esp32s3 -B C:/build/q2-s3 -D SDKCONFIG=C:/build/q2-s3/sdkconfig -D Q2_KERNEL_DIR=C:/path/to/kernel build
```

需要 SDK 的 ESP32-S3 Xtensa 工具链和 Python；本次使用工作区 ESP-IDF v6.2-dev。
第三方 Lua/LVGL/字体来自独立 Kernel 的固定版本，不使用旧 firmware 的运行代码或在线组件下载。
平台目录 SDK 代码与 Kernel 的注册接口分离；Kernel 仅增加可覆盖的分配器配置并修正可移植类型问题。
不会自动烧录。Flash 分区为单 factory APP，空间 15 MiB，包含约 9.44 MiB 中文字体；无 OTA 槽。

## 内存与刷新

LVGL 使用 IDF 标准堆分配器，由 SDK 将大分配放入 PSRAM；不在内部 SRAM 放置 PC 的 16 MiB 静态池。
显示额外保留 329,728 字节 PSRAM RGB565 原生帧缓冲，另有 7,360 字节内部 DMA 分块缓冲（10 行）。
只扩展 dirty rectangle 到偶数边界，相邻像素来自保留画面，避免读取源缓冲之外或丢失邻居像素。
ESP LCD QSPI 完成回调唤醒 owner 后才复用 DMA 缓冲，符合 Kernel 像素后端的同步契约；ISR 不调用 Kernel/Lua。
I²C 触摸采集/重试只在 q2_touch 任务运行；Kernel、Lua 和 LVGL 仍只由 app_main owner 操作。
采集任务通过线程安全 FIFO 交付样本，不直接调用 UI。屏传输失败停止 Kernel 并输出诊断。

## 验证与实机验收

- `tests/test_geometry.c`：遍历两种方向全部像素，验证触摸/显示坐标互逆、越界和释放、RGB565。
- `tests/test_demo.py`：通过 PC 的真实 Kernel/Lua/LVGL 执行同一内嵌 Lua，验证横竖屏按钮和颜色。
- ESP32-S3 实际交叉编译、分区和固件尺寸检查。

上述不等于已上板：还需确认模块内存型号，再烧录验证 RGB 顺序、四角触摸、圆角裁切、
屏幕供电时序以及连续刷新。日志必须显示 `CO5300 AM178 ... CHSC6417=ready`；
若 unavailable，先核对 I²C 地址、电平和上拉。

## 来源

初始化命令和 CHSC6417 报告格式依据
[OSPTEK AM178 示例 ffe29d6746f1529e3b03041fd6887e1c653f4c33](https://github.com/osptek/amoled-1.78-368x448-qspi-co5300/tree/ffe29d6746f1529e3b03041fd6887e1c653f4c33)，
Copyright 2026 OSPTEK；仓库 MIT，资料标注 CC-BY-4.0。
改动：用户 GPIO、原生/软件旋转、同步 SPI、受限分块、Kernel 像素注册和 owner 触摸转发。
QSPI 命令/像素操作码参考 Espressif esp_lcd_co5300 2.2.0（Apache-2.0）协议。
原始资料保留在旧项目 third_party/osptek-am178 与 managed_components/espressif__esp_lcd_co5300，
本目录没有复制整套第三方驱动，未修改 Kernel 的 vendor 文件。


## 字体编译开关

产品配置 `q2.esp32s3.json` 的 `display.cjk_font` 默认 true，保留中文歌名显示。
设为 false 后执行根目录 `bash build.sh`，移除大字库和 Tiny TTF，保留英文、数字和图标。
独立 ESP-IDF 构建使用 `idf.py -D Q2_ENABLE_CJK_FONT=OFF build`；
根入口以 JSON 为准。此开关不修改 Music 的歌词设置。

## 间歇性 NACK 诊断

ESP-IDF 的 `ESP_ERR_INVALID_RESPONSE` 在当前 I²C 同步事务路径表示 NACK。
初始化 `CHSC6417=ready` 只证明地址探测和 IRQ 注册成功，不代表坐标读取已验证。
首次成功读取另输出 `CHSC6417 first report`；短暂错误重试成功输出 `touch retry recovered`。
失败耗尽输出 `touch read exhausted 3 attempts`，附 INT 电平、读取、重试和失败计数。
恢复/耗尽诊断最多每 5 秒一次；错误仍返回给 owner，不因限流变成成功。

`tests/test_touch.c` 用注入 I/O 结果验证：无 IRQ 不访问总线、保留触摸、无新 IRQ 的重试、
松手报告重试、新 IRQ 不刷新预算、连续失败释放和下次 IRQ 恢复。
在 q2 根目录可运行：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ikernel/include -Iplatform/esp32s3/main \
  platform/esp32s3/main/am178_geometry.c platform/esp32s3/main/am178_touch.c \
  platform/esp32s3/tests/test_touch.c -o build/touch-retry/test_touch
build/touch-retry/test_touch
```

这修复了偶发通信错误直接转成松手的处理缺陷；NACK 的电气/时序根因仍需实机验证。
若持续耗尽，优先检查 TP 电源、SDA/SCL 外部上拉及波形、排线接触和 INT 噪声，不能仅靠更多重试判断硬件正常。

## 快速点击采集与回放

此前只在 owner 循环读取 TP，屏幕同步刷新或渲染期间的多个 IRQ 会合并成一个请求；
CHSC6417 寄存器只提供当前报告，稍后增加读取次数无法恢复已经被覆盖的按下/松手。
现在独立任务及时采集，将**已读取到的**状态变化存入 32 项 FreeRTOS FIFO。
主循环每轮最多顺序回放 16 个样本，然后以 16 的事件预算 poll Kernel。
空 FIFO 返回 Q2_ERR_NOT_FOUND，不代表松手；无新样本时 LVGL 保留输入状态。

重复状态不入队，移动坐标在容量紧张时可合并，保留至少 8 个空槽用于边沿。
按下/松手不会被新状态覆盖；真正满队列时采集任务等待消费并明确告警，因而极端拥堵仍可能漏读硬件报告。
关闭时先禁用 GPIO ISR，再请求任务退出，等待正在进行的 I²C/队列操作结束，最后删除任务及资源。

诊断 `touch capture down=... up=... queued=... stalls=...` 表示已入队的状态变化与积压，
并非 GPIO IRQ 的计数；stalls 为满队列等待次数，正常快速点击应保持 0。
若屏幕 Touches 计数落后但 down/up 都完整，应继续检查消费/UI；若 down/up 本身缺少，
需结合 retry/exhausted 日志检查 I²C 和 TP/INT 波形。计数包括故障耗尽后的释放。

软件验证新增真实 Lua/LVGL 批量回放：每批 8 次按下/松手，4 批共 32 次点击全部送达，
并在 handler 中修改背景以触发绘制。该测试不等于硬件高速点击已验证。

## 屏幕亮度诊断控件

内嵌测试界面提供 `- 10%` / `+ 10%` 按钮，范围 10–100%，重启恢复 30%。
完整沿用本地厂商初始化表（包含 0xA0），初始化完成后再设置为 30%。
百分比表示寄存器设置比例，不是实测亮度或功耗比例；此修改不证明供电问题已解决。

Platform 注册 `panel_control` 组件，通过现有 Kernel 动态方法机制提供
`q.panel.brightness()` / `q.panel.brightness(percent)`，要求 `display.brightness` 能力。
底层范围 0–100，控制界面保留 10% 下限以便操作。成功返回已发送设置，失败不更新显示值，
显示 ERROR 并保留 APP；SPI 发送成功并非屏幕已接收的读回确认。
写寄存器与刷屏均在 owner 执行，不从 TP 采集任务访问 SPI。设置不写入 Flash。
PC 测试没有此硬件 provider 时显示 unavailable，RGB 按钮仍可使用。
日志 `brightness=...% register=0x...` 可用于对比不同亮度下黑屏情况。

### 亮度组件启动回归

亮度组件必须提供 Kernel 要求的 start/stop 回调；硬件已由 owner 初始化，组件仅借用面板，
stop 不销毁面板。注册描述符存活至 Kernel 销毁，随后 owner 释放 LVGL 和面板。
启动失败时仅销毁已创建的对象，避免空 Lua runtime 的销毁错误阻止后续清理。
`tests/test_controls.c` 使用真实 Kernel 和生产注册代码，仅替换物理 SPI 访问，覆盖缺失
生命周期回调的旧故障、启动成功、权限与参数拒绝、I/O 失败、正常及 boot 前清理。

```sh
cc -std=c11 -Wall -Wextra -Werror -DQ2_ENABLE_LOG=0 -Ikernel/include -Iplatform/esp32s3/main \
  kernel/src/core/kernel.c platform/esp32s3/main/am178_controls.c \
  platform/esp32s3/tests/test_controls.c -o build/brightness/test_controls
build/brightness/test_controls
```

## 厂商 demo 显示链路对齐

显示传输现使用 ESP-IDF `esp_lcd` QSPI IO，配置和初始化顺序依据 OSPTEK demo。
新增完成日志 `LCD DMA complete ...`，保留亮度控制与现有 TP 采集。
具体差异、上游版本、许可和实机排查方法见 [驱动对照](docs/am178-vendor-alignment.md)。
协议回归测试（在 q2 根目录）：

```sh
cc -std=c11 -Wall -Wextra -Werror -Ikernel/include -Iplatform/esp32s3/main \
  platform/esp32s3/main/am178_panel.c platform/esp32s3/tests/test_panel.c \
  -o build/am178-vendor/test_panel
build/am178-vendor/test_panel
```

## Lua/LVGL 诊断入口（当前未启用）

关闭 `CONFIG_Q2_AM178_DIRECT_RGB` 后，入口运行 `main/display_demo.lua`：启动显示纯红（FF0000），每 2 秒切换
纯绿（00FF00）、纯蓝（0000FF），反复循环，无按钮、文字及触摸控制。
使用 Lua 的单调时间 on_tick 调度，不阻塞等待；延迟时只切换一次，避免跳过可见颜色。
屏幕寄存器亮度仍默认 30%。触摸采集继续运行，但不参与颜色切换。
串口 `RGB requested: GREEN step=2` 表示 APP 请求刷新，可配合 `LCD DMA complete` 定位时机，
两者均不是面板实际显示成功的确认。

原手动 RGB/亮度界面保留在 `main/brightness_demo.lua`，其原有测试仍针对该文件。
这次切换诊断界面本身不宣称黑屏已修复。

## 当前默认入口：C 整帧直刷定位冻结

`Q2 AM178 display diagnosis → Run direct full-frame RGB test` 当前默认开启。
当前已有 build/linux/esp32s3/sdkconfig 也已启用，新旧构建均运行 C 直刷入口。
启动输出 `DIRECT RGB mode`，C 每 2 秒循环纯红/绿/蓝，不启动 Kernel/Lua/LVGL 或触摸采集。
该可选模式同样使用 30% 亮度和当前 10MHz QSPI 配置。
当前每帧由多个原生 10 行窗口覆盖，每个窗口使用 10 行内部 DMA 缓冲，逐块等待完成；不直接发送 PSRAM 数据。日志包含源颜色、RGB565 和完成颜色。

此模式是定位工具，不代表原来的冻结已修复。若 RAW RGB submit/DMA complete 都持续变色而
屏幕仍固定红色，优先检查 LCD 端通信、屏幕状态及供电；如果直刷稳定，再比较 LVGL 分块链路。
menuconfig 关闭上述选项可恢复 Lua RGB 模式，并新增 `LVGL payload rgb888=... rgb565=...` 日志。
由于过去 DMA 日志 5 秒限流、RGB 每 2 秒切换，日志可能每 6 秒恰好打印红色，不能据此判定
绿色/蓝色未刷新。新 payload 日志不受该限流影响。

## 本地参考工程对齐（2026-10-10）

本次参考用户 `/home/alon/github/amoled-1.78-368x448-qspi-co5300/versions/AM178Q368448LK/examples/esp32s3-amoled-1.78-368x448-qspi-co5300-bringup`，
该 checkout 含本地修改，与远程原版不同。实际采用 GREEN 引脚、10MHz QSPI、10 行内部 DMA 缓冲、
队列深度 10。Q2 已对齐这三项传输配置，并把 LCD 复位移到 QSPI IO 初始化之后。
保持 Q2 同步像素后端契约：每块 DMA 完成后才复用缓冲；窗口偶数对齐，横屏由软件映射。

当前 Q2 没有调用 `esp_lcd_panel_swap_xy`、`esp_lcd_panel_mirror` 或 LVGL 硬件旋转接口。
仅在初始化写一次 MADCTL=0，跟本地参考驱动一致；不能把原版 demo 的 unsupported 旋转日志
当作 Q2 当前故障的已证实原因。`test_panel.c` 增加了方向寄存器的回归断言。

有意保留的差异：30% 亮度（参考工程启动最终 FF）；原生有效窗口；单缓冲同步提交；
10 行 SPI 事务上限（参考总线上限为整帧，但它的 LVGL 实际提交 10 行）；
独立 TP 任务和受限重试。Flash/PSRAM 型号、频率和分区没有因屏幕问题一并更换。

当前固件使用默认开启的 C 直刷诊断；关闭该 menuconfig 选项可恢复 Kernel/Lua/LVGL 自动 RGB。
启动应显示 `QSPI 10 MHz / ESP LCD` 和 `tile_rows=10 DMA_bytes=7360 queue=10`。
编译与软件测试通过不能代替持续运行实测；仍需观察长时间 RGB 循环是否停画面。

## 当前驱动：原版 OSPTEK 面板组件

Q2 已接入本地稳定参考工程中的原版 `esp_lcd_qspi_amoled` 面板组件和完整初始化表，
不再使用之前的 `am178_panel.c` 手写协议流程；原文件保留作历史对照但不参与固件链接。
实际差异、许可证和验证范围统一见 [当前适配说明](docs/am178-vendor-alignment.md)。
启动标识为 `OSPTEK panel driver: tile_rows=10 DMA_bytes=7360 queue=10`。

### C 直刷诊断当前设置

每次完整 DMA 结束后保持画面两秒，顺序 RED → GREEN → BLUE，无需触摸。
启动标识 `DIRECT RGB mode; no Lua, LVGL or touch; 2s per color`。
不创建 Kernel、Lua runtime、LVGL 或 TP 采集任务，也没有屏保。
使用指定本地厂商完整初始化（0x58=07）、最终 0xFF/100% 亮度、10MHz QSPI 和内部 DMA 缓冲。
每帧原生 368×448，10 行一块，共 45 块，329728 字节；最后一块 8 行。
`RAW RGB DMA complete` 记录颜色、累计完成字节、块数和耗时，不是屏幕显存读回。
当前版本已交叉编译，但显示结果仍需实机确认。

当前初始化来源及历史诊断区别见 [驱动对照](docs/am178-vendor-alignment.md) 末节；
当前固件亮度为本地原厂初始化的 100%，之前章节中的 30% 是历史诊断配置。
