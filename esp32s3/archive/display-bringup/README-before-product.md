# ESP32-S3 / AM178 平台

本目录现在是真正的 ESP32-S3 构建目标，接入 Q2-Kernel、Lua API 2、LVGL 中文字体、
CO5300 QSPI 显示和 CHSC6417 单点触摸。启动时运行内嵌 display-demo，全屏纯红→纯绿→纯蓝，每 2 秒自动切换并循环。
没有按钮和文字，颜色切换不依赖触摸；用于复现显示黑屏。
目前未移植工厂 APP 管理、Flash 私有存储、Wi-Fi/HTTP、音频、蓝牙、USB 或物理按键；
这些能力不会注册成可用。PC Music APP 不能仅靠此次移植直接在设备上联网播放。

## 硬件与配置

参考配置为 **ESP32-S3 N16R8：16 MiB Flash / 8 MiB 八线 PSRAM**，这是待确认的构建假设。
默认原生竖屏 368×448；完整产品通过根 `q2.esp32s3.json` 的宽高及旋转角度配置。
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

## 屏幕旋转

产品配置使用根 `q2.esp32s3.json` 的 `display.rotation`，单位度数。
角度表示从原生方向将设备顺时针转动后的观看方向；0° 为原生面板坐标，90° 保持原横屏映射。
像素软件转换到原生显存，触摸执行逆映射；不调用厂商不支持的 swap_xy/mirror。

| rotation | width | height |
|---:|---:|---:|
| 0 | 368 | 448 |
| 90 | 448 | 368 |
| 180 | 368 | 448 |
| 270 | 448 | 368 |

例如：`"display": {"width": 448, "height": 368, "rotation": 270, "cjk_font": false}`。
宽高是旋转后 APP 的逻辑尺寸，方向不匹配会在构建配置阶段报错。修改后重新编译烧录。
旧文件未配置 rotation 时兼容原逻辑：368×448 默认为 0°，448×368 默认为 90°。
独立 Platform 构建（未注入 Q2_GENERATED_DIR）可在 menuconfig 的
**Q2 AM178 standalone display orientation** 选择四个方向，逻辑宽高自动匹配。
产品构建以 JSON 为准，忽略该独立构建选项。

启动日志打印 `software rotation=...`。Lua 和面积测试共用旋转后的后端。
纯色全屏或居中色块不适合目视分辨 0/180 与 90/270；应使用有文字/不对称位置的 UI 验证。
旋转测试遍历四方向全部像素，验证一一映射、独立四角预期和触摸逆变换。

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

需要 SDK 的 ESP32-S3 Xtensa 工具链和 Python；使用工作区激活的 ESP-IDF 环境。
第三方 Lua/LVGL/字体来自独立 Kernel 的固定版本，不使用旧 firmware 的运行代码或在线组件下载。
平台目录 SDK 代码与 Kernel 的注册接口分离；Kernel 仅增加可覆盖的分配器配置并修正可移植类型问题。
不会自动烧录。Flash 分区为单 factory APP，空间 15 MiB，字体大小取决于当前字体开关与裁剪配置；无 OTA 槽。

## 内存与刷新

LVGL 使用 IDF 标准堆分配器，由 SDK 将大分配放入 PSRAM；不在内部 SRAM 放置 PC 的 16 MiB 静态池。
显示额外保留 329,728 字节 PSRAM RGB565 原生帧缓冲，另有 7,360 字节内部 DMA 分块缓冲（10 行）。
只扩展 dirty rectangle 到偶数边界，相邻像素来自保留画面，避免读取源缓冲之外或丢失邻居像素。
ESP LCD QSPI 完成回调唤醒 owner 后才复用 DMA 缓冲，符合 Kernel 像素后端的同步契约；ISR 不调用 Kernel/Lua。
I²C 触摸采集/重试只在 q2_touch 任务运行；Kernel、Lua 和 LVGL 仍只由 app_main owner 操作。
采集任务通过线程安全 FIFO 交付样本，不直接调用 UI。屏传输失败停止 Kernel 并输出诊断。

## 验证与实机验收

- `tests/test_geometry.c`：遍历两种方向全部像素，验证触摸/显示坐标互逆、越界和释放、RGB565。
- `tests/test_demo.py`：通过 PC 的真实 Kernel/Lua/LVGL 执行同一内嵌 Lua，验证保留的 brightness_demo.lua 横竖屏按钮和颜色。
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
当前面板组件来源及本地参考差异见 [驱动对照](docs/am178-vendor-alignment.md)。


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

## 唯一启动亮度配置

产品只修改根 `q2.esp32s3.json` 的 `display.brightness`（0–100 整数百分比）。
当前按用户要求设为 **100**，换算后 `0x51=0xFF`。
`main/am178_config.h` 读取生成配置，初始化表引用它，并且只写一次 `0x51`；
已移除初始化完成后的 100% 强制覆盖。不要直接在初始化表里写死亮度值。
独立 Platform 构建使用 Kconfig 的 `Q2_AM178_INITIAL_BRIGHTNESS`；产品构建忽略它，以 JSON 为准。

`q.panel.brightness(percent)` 继续用于运行时主动调节，未调用时保持启动配置。
亮度缓存只在初始化/写入成功后更新，读取缓存不代表硬件寄存器读回；重启恢复 JSON 设置。
板级寄存器参数集中在 `main/am178_vendor_init.h`，用户修改的 `0x63=0x80` 保留。
旧 `am178_panel.c/.h` 和仅测试旧实现的 `test_panel.c` 已移除，避免误改无效代码。
原文件备份在产品 `build/init-cleanup/legacy/`，不参与构建。

`python3 platform/esp32s3/tests/test_init.py` 编译真实初始化表，覆盖 0/25/100% 的寄存器值、
单次亮度写入及产品配置优先级；不模拟面板显示效果。

## 显示测试入口

menuconfig → **Q2 AM178 display diagnosis → Startup mode** 提供四个互斥入口：

- **Lua display demo**：干净构建默认入口，Kernel/Lua/LVGL 自动循环 FF 红绿蓝。
- **Black-background area test**：可选入口，42 个 A0/FF 面积对照，
  无 Lua/LVGL/触摸；详细步骤和结果判断见 [面积测试](docs/am178-area-test.md)。
- **Blue pixel bits / 10MHz vs 2MHz test**：小面积 A0/A8/E0/E8/F0/F8/FF/08 蓝色、两组时钟，
  每项重新复位，整轮使用配置的启动亮度；见 [蓝色数据位测试](docs/am178-blue-test.md)。
- **Direct C full-screen A0 RGB cycle**：保留用户已确认可显示的 A0 全屏对照，每色保持两秒。

各入口共用同一初始化表与项目启动亮度；当前为 100%（0x51=0xFF）、0x63=0x80、0x58=07、10 MHz QSPI。
初始化后不会再强制写入 FF/100%。蓝色数据位测试单独比较 10 MHz 与 2 MHz。
已去掉常规刷新中的 payload、逐帧统计及 UI 状态诊断日志，保留传输超时/触摸错误处理。
DMA 同步、ISR 唤醒、GPIO17 复位冲突修正及触摸采集修复未撤销。
原亮度按钮界面仍保存在 `main/brightness_demo.lua`，当前自动 RGB 页面没有亮度按钮。
`panel_control` 的 `q.panel.brightness([percent])` API 继续保留。

新增 `tests/test_area.c` 覆盖生产图案生成/渲染函数：42 步、两种方向、所有像素、
黑色清屏和固定整帧传输、越界拒绝及后端错误传播。
以前的诊断记录归档在 `docs/archive/`，其中参数不代表当前配置。

## 初始化与参数变更日志

ESP-IDF INFO 日志默认显示：

- `INIT begin`：逻辑分辨率、软件旋转、QSPI 时钟、DMA 分块与队列。
- `INIT applied driver prefix`：当前 RGB/RGB565 配置对应的驱动前置 MADCTL/COLMOD。
- `INIT applied [序号/总数]`：实际初始化表的命令、参数字节数、延时；下一行十六进制数据。
  零参数命令没有数据行。仅厂商初始化成功后输出整表；失败显示 `INIT incomplete`。
- `PARAM software gap` / `PARAM display_on`：初始化后的偏移设置及显屏命令结果。
- `PARAM applied cmd=0x51`：运行时亮度旧值/新值、旧百分比/新百分比。
  写失败为 `PARAM failed`，保留旧缓存并打印目标值及错误。

这些是 MCU 发送结果及软件缓存，不是屏幕寄存器读回。
`WINDOW` 为 DEBUG 级，每次刷新打印原生地址范围（含 X 偏移）、像素字节数及传输结果。
默认 INFO 不逐块打印窗口。ESP-IDF 日志等级控制这些驱动日志；根 logging 开关控制 Q2 日志。
开启窗口 DEBUG 会影响传输节奏，仅用于短时定位。

## AM213 2.13 英寸支持

可通过根配置 `q2.esp32s3-am213.json` 选择 AM213（410×502、CO5300、CST820、X gap 22）。
上文 AM178 接线沿用，尺寸/触摸协议由型号选择；详见 [AM213 配置、构建和验证](docs/am213.md)。
