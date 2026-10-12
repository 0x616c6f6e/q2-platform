> Historical snapshot; settings below are not the current defaults.

# AM178 显示适配：本地已验证参考工程

用户确认本地 OSPTEK demo 在同一块开发板、屏幕、供电接线下能持续正常运行；
Q2 在 GREEN 请求后仍发生黑屏，日志已证明源像素为 RGB888=00FF00、RGB565=07E0。
因此本次优先去掉 Q2 面板协议实现与稳定参考的差异，不能将未实测的改动宣称为故障已修复。

参考路径：
`/home/alon/github/amoled-1.78-368x448-qspi-co5300/versions/AM178Q368448LK/examples/esp32s3-amoled-1.78-368x448-qspi-co5300-bringup`

上游 https://github.com/osptek/amoled-1.78-368x448-qspi-co5300 ，提交
`ffe29d6746f1529e3b03041fd6887e1c653f4c33`，该本地 checkout 的板级代码有用户修改。

## 当前实现

- `components/osptek_qspi_amoled` 的 C 驱动、头文件和 Apache-2.0 许可证按字节原样复制。
  仅 CMake 使用 Q2 本地依赖，不需要下载组件。来源/哈希在该目录 `upstream.json`。
- `main/am178_vendor_init.h` 当前与指定本地 `lcd_init.c` 的 13 项初始化表完全一致；仅更名和调整空白/注释。
  包括初始窗口、A0 亮度和各阶段延时，没有重新排序或修正其中的窗口参数。
- 采用参考调用链：创建面板 → reset → init → gap(16,0) → display_on(true) → 最终亮度。
  draw_bitmap 按每次像素区域设置原生有效窗口，不调用硬件 swap_xy/mirror。
- 10MHz、10 行内部 DMA 缓冲、队列 10、整帧总线最大传输上限均与本地参考对齐。
  每块实际提交最多 7360 字节并等待完成；原始 RGB 诊断也改用内部 DMA 分块，不传 PSRAM。
- 保留 Q2 的 XRGB8888→RGB565 转换和软件横屏映射，亮度最后设为 100%（FF），与指定本地工程一致。
  初始化表先设置 A0，随后最终覆盖为 FF，与本地工程顺序一致。
- Kernel 像素后端要求同步返回，所以等待厂商异步 DMA 的完成信号再复用缓冲。
  没有复制参考工程的 LVGL 线程/锁；Kernel、Lua、LVGL 仍由同一 owner 执行。
- 保留 Q2 独立触摸采集及受限重试。触摸与 LCD 复位分开。
- `am178_panel.c/.h` 及旧 test_panel 是上一版协议对照留档，已从固件构建移除；
  它们的通过结果不能作为当前厂商驱动运行验证。

## 验证

复制的三份厂商文件与本地参考逐字节一致；全部 13 项初始化命令、参数及延时一致。
ESP32-S3 交叉编译通过，ELF 链接了 esp_lcd_new_panel_qspi_amoled、panel_qspi_amoled_draw_bitmap。
几何、触摸、组件与项目契约检查保留。此处没有进行实机烧录，也没有确认黑屏根因。

启动应包含 `OSPTEK panel driver: tile_rows=10 DMA_bytes=7360 queue=10`。
Lua RGB 仍每 2 秒循环；`LVGL payload` 表示转换前后像素，`LCD DMA complete` 仅确认 MCU 传输完成。
若继续黑屏，保留完整日志，并比较本地参考相同全屏纯色负载；不能把 DMA 完成当作屏幕接收确认。

## 许可与变更

面板组件 Copyright 2023 Espressif，Apache-2.0 原文随组件保存，无源代码修改。
板级初始化表来源 Copyright 2026 OSPTEK；仓库软件 MIT，资料标注 CC BY 4.0，
[来源](https://github.com/osptek/amoled-1.78-368x448-qspi-co5300)和以上改动说明保留署名，
MIT 文本见 `osptek-MIT.txt`。没有改动用户参考 checkout 或 Kernel 第三方库。

## 仍黑屏后的同负载对照（2026-10-10）

用户进一步实测：原版面板组件下仍在 GREEN 后黑屏；正确颜色和 DMA 完成不能证明屏幕显示成功。
`gpio: conflict found for GPIO[17]` 来自 adapter 与面板组件重复配置 LCD_RST。
已移除 adapter 的 LCD_RST 配置/拉低操作，由原版面板组件独占复位；这是告警修正，尚不能认定为黑屏修复。

独立对照工程位于产品根目录 `build/am178-reference-rgb/source`，烧录说明在同级 `README.md`。
它复制用户本地稳定厂商工程，保留原 LVGL、双内部 DMA 缓冲、触摸及 SDK 配置，
仅将音乐页面改为每两秒 RGB 全屏循环，并将初始化最终亮度 FF 改为 4D（30%）。
亮度在原有初始化位置设置，不在 LVGL 启动后插入未加锁的 SPI 操作。
原工程保持不变，`reference.json` 记录原文件哈希。尚未进行实机对照。

对照不是 Q2 的替代实现：它先区分“原厂同负载也失败”与“仅 Q2 刷新链路失败”。
前者需继续检查共同的面板/供电/通信条件；后者继续比较 Q2 的缓冲、同步和刷新节奏。
两种结果都不能单独锁定某个根因。不要只因原厂音乐页面稳定就排除全屏负载差异。

## 触摸影响黑屏的屏保排查

用户补充 Q2（RGB requested 日志）持续触摸不熄灭，但蓝色不可见；停止触摸后黑屏。
现有 ESP32-S3 resolved.json 的 screensaver=false，入口仅启动 RGB 单 APP，没有 lifecycle APP，
LVGL provider 的零初始化 system_config 原本也关闭屏保。不能仅凭该现象确认是屏保。

诊断入口现显式调用 q2_lvgl_system_configure 关闭屏保和状态栏，并打印
`RGB_DIAG_V2 screensaver_enabled=0 status_bar_enabled=0`。
每 5 秒通过现有公开 API 输出 `UI state: saver=... locked=... lifecycle=... touch=... idle_ms=... brightness_cached=...`。
这些操作只读取状态，不调用 keep_awake、不伪造触摸、不重写亮度。
brightness_cached 是成功写入值的缓存，并非屏幕寄存器读回。

隔离测试 `build/am178-idle-check/test_rgb_idle.c` 加载实际 display_demo.lua，使用生产
Kernel/Lua/LVGL 和 panel_control 注册代码，仅用捕获像素后端/亮度桩代替硬件。
180 秒虚拟时间覆盖 90 秒无输入、30 秒连续点击、60 秒停止输入，逐像素检查采样全屏颜色。
三阶段红绿蓝采样数分别为 30/30/30、10/10/10、20/20/20；saver/locked/lifecycle 始终为 0，
没有亮度写入。宿主验证不覆盖 ESP32 时序、实际 QSPI、供电或面板行为。
ESP32-S3 编译与项目契约检查通过，尚未实机确认；黑屏原因仍待运行状态日志和对照测试确定。

## 蓝色阶段保留绿色：DMA 唤醒与整帧审计

当前本地 ESP-IDF `components/esp_lcd/spi/esp_lcd_panel_io_spi.c` 的
`lcd_spi_post_trans_color_cb` 调用用户完成回调但丢弃其 bool 返回值。
Q2 此前仅返回 xSemaphoreGiveFromISR 的 wake 标志，未显式触发调度；
在 CONFIG_FREERTOS_HZ=100、逐块同步等待下，owner 可能等待下一节拍才继续。
现在 color_done 中 wake 为真时显式 portYIELD_FROM_ISR，再返回 false，
兼容本地回调行为且不修改 SDK。它修正任务唤醒路径；尚不能宣称蓝色缺失/黑屏实机修复。

新固件启动标识：`RGB_DIAG_V3 screensaver_enabled=0 dma_isr_yield=1`。
新增 `FRAME DMA`：仅统计从顶部开始、连续完整宽度的刷新区域；
所有相应 DMA 完成之后才累计字节和 band 数。混合内容用 uniform=0 标记。
AM178 竖屏纯色全帧应 rows=448/448、bytes=329728；32 行 LVGL/10 行 DMA 配置下为 56 bands。
蓝色应 rgb565=001f、wire=00,1f、uniform=1。wire 是软件 DMA 缓冲中的预期字节表示，
不是逻辑分析仪测量，也不是面板显存读回。us 为整帧从第一块开始至最后块完成的耗时。
非连续或局部更新不打印整帧完成，不能把没有该日志的局部 UI 刷新误判为发送失败。

ESP32-S3 编译、契约检查、现有 RGB 空闲宿主测试及坐标/颜色转换测试通过；
宿主测试不模拟实际 ISR 调度，刷新耗时改善和蓝色显示仍需要板端日志验证。

## V4：只关闭色彩增强的诊断

用户 V3 日志已记录 RED/GREEN/BLUE 连续完整帧，每帧 448 行、329728 字节、56 bands，
耗时约 265ms；saver=0、locked=0、lifecycle=0。至少在这些记录点，屏保未启动，
蓝色没有在 Lua/LVGL 或软件 DMA 完成统计中丢失。软件日志不等于屏幕显存读回。

当前 V4 唯一改变的面板参数为初始化 0x58: 07 -> 00（关闭色彩增强）。
CO5300 V0.00 手册第 192 页描述该寄存器的 SLR_EN 与 SLR_LEVEL，
所列等级为 0/1/2，而厂商使用的 07 含等级 3。不同修订/厂商调校仍可能允许该值，
不能单凭这份手册宣布厂商参数错误。此修改是排查试验，不是已验证的修复。
原始厂商工程和 build/am178-reference-rgb 的对照副本都保留 07，不改其基线。

来源：https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf
对比驱动：https://github.com/kodediy/esp_lcd_co5300/blob/main/esp_lcd_co5300.c
启动标识：`RGB_DIAG_V4 ... enhancement_58=00`。
若无改善，恢复 07 并使用此前已编译的原厂 RGB 同负载对照固件，避免继续叠加猜测性改动。

## 按用户要求切换 C-only RGB

默认启用 CONFIG_Q2_AM178_DIRECT_RGB（Kconfig、sdkconfig.defaults 和当前构建 sdkconfig）。
app_main 在硬件检查后调用 direct_rgb_demo，不执行下面的 Kernel/Lua/LVGL 启动路径。
每次直接提交 native 全屏颜色，等待各块 DMA 完成，然后保持两秒；重复 RED/GREEN/BLUE。
保留现有面板参数。TP 采集关闭；整帧不经过 LVGL 32 行拆分，因此改为 45 个 10 行 DMA 窗口。
这隔离上层渲染，但不构成蓝色缺失根因的证明。RAW RGB 日志包含完整帧完成统计。

## 当前：恢复用户指定本地工程的初始化

以用户指定的本地 bringup/main/app_init/lcd_init.c 为准重新复制完整 13 条命令。
撤销 V4 的 0x58=00 试验，恢复 0x58=07；最终亮度也恢复 app_lcd_init() 的 0xFF（100%）。
已核对命令、参数、参数长度和延时全部一致，原版面板 C 驱动/头文件/许可证逐字节一致。
SPI2、mode 0、10MHz、32-bit command、8-bit parameter、quad、queue 10、gap(16,0) 保持参考配置。
创建面板 → reset → init → gap → display on → brightness FF 的调用顺序一致。

当前 C 直刷 RGB 模式仍开启，不启动 Lua、Kernel、LVGL 或触摸采集。
完成回调保持 Q2 的逐块同步等待和显式 ISR 唤醒，以安全复用 DMA 缓冲。
启动新增标识：`LOCAL OSPTEK init matched: 13 commands; gap=16,0; brightness=0xff; enhancement=0x07`。
这确认使用的初始化版本，不表示已实机确认黑屏/蓝色问题解决。

## 原厂完整运行路径 RGB 副本

用户要求将稳定厂商工程的音乐界面替换为相同 RGB 循环。
独立工程：`/home/alon/github/q2/build/am178-vendor-rgb/source`，构建目录为同级 `build`。
其 main/main.c 每两秒在原厂 app_lvgl_lock 下设置全屏背景色；保留原厂 app_init() 的
LCD、触摸、LVGL 任务和双缓冲路径。与指定本地原工程对比，复制源文件仅 main/main.c 不同。
最终亮度仍 FF，初始化 0x58 仍 07；它不同于之前降低到 30% 的参考试验。
`reference.json` 记录原文件哈希，`main.c.original` 保存原音乐入口。
启动标识 VENDOR_RGB_BASELINE，烧录说明见该目录 README.md。原厂原目录不修改。
该副本包含原厂 LVGL（用于对比完整稳定路径），Q2 当前固件仍是 C 直刷且不运行 LVGL。
