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
- `main/am178_vendor_init.h` 来源为指定本地 `lcd_init.c` 的 13 项初始化表。
  当前有意保留用户修改的 0x63=0x80，0x51 由唯一项目亮度配置生成；窗口与命令顺序/延时保留。
- 采用参考调用链：创建面板 → reset → init → gap(16,0) → display_on(true)。亮度只由初始化表写入一次。
  draw_bitmap 按每次像素区域设置原生有效窗口，不调用硬件 swap_xy/mirror。
- 10MHz、10 行内部 DMA 缓冲、队列 10、整帧总线最大传输上限均与本地参考对齐。
  每块实际提交最多 7360 字节并等待完成；原始 RGB 诊断也改用内部 DMA 分块，不传 PSRAM。
- 保留 Q2 的 XRGB8888→RGB565 转换和软件横屏映射，启动亮度由 `display.brightness` 配置。
  当前 100% 对应 0xFF，已移除参考工程的后置 FF 覆盖，避免两处参数相互覆盖。
- Kernel 像素后端要求同步返回，所以等待厂商异步 DMA 的完成信号再复用缓冲。
  没有复制参考工程的 LVGL 线程/锁；Kernel、Lua、LVGL 仍由同一 owner 执行。
- 保留 Q2 独立触摸采集及受限重试。触摸与 LCD 复位分开。
- 旧 `am178_panel.c/.h` 及仅针对旧实现的 test_panel 已移出源码树；
  历史备份位于产品 build/init-cleanup/legacy，不参与编译。

## 当前测试与清理

当前初始化亮度从唯一配置读取（100%/0xFF），0x63=0x80、0x58=07。
蓝色诊断默认 QSPI RGB888：0x3A=0x77、bits_per_pixel=24、每像素3字节同步选择；
关闭诊断格式开关恢复RGB565。其他启动模式保持RGB565，见 [蓝色诊断](am178-blue-test.md)。
Lua 自动 RGB 是干净构建默认值；面积测试为可选入口。具体流程见 [面积测试](am178-area-test.md)。
用户实测 A0 正常、FF 复现异常；这缩小了排查范围，但尚不能证明供电或驱动中的某一项是根因。
软件 DMA 完成不是面板显存读回，也不等于人眼确认显示正确。

已删除正常刷新里的 `LVGL payload`、`LCD DMA complete`、`FRAME DMA` 和周期 UI 状态日志。
保留同步 DMA 缓冲生命期、完成 ISR 的显式调度、由厂商面板独占 GPIO17 复位、
触摸独立采集/重试/FIFO。这些是独立正确性修复，不随诊断日志撤销。

## 许可

面板组件 Copyright 2023 Espressif，Apache-2.0 原文随组件保存，无源代码修改。
板级初始化表来源 Copyright 2026 OSPTEK；仓库软件 MIT，资料标注 CC BY 4.0，
[来源](https://github.com/osptek/amoled-1.78-368x448-qspi-co5300)及以上变更说明保留署名。
MIT 文本见 `osptek-MIT.txt`。未修改用户参考 checkout 或 Kernel 第三方库。
历史诊断见 `archive/am178-diagnostics-before-area-test.md`，不作为当前参数说明。
