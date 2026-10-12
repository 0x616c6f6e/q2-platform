# ESP32-S3 正式应用平台

AM213 已由用户确认显示与触摸正常。固件现在启动常驻 **Homepage**，内置 **System settings**；点击图标进入设置，通过 Home 按钮返回主页。源码仍来自独立 Q2-App，没有复制一套板端 UI。显示诊断入口、RGB 轮播、面积测试及实验 SPI 传输已退出构建，历史快照见 [归档](archive/display-bringup/README.md)。

## 使用

在产品根目录激活 ESP-IDF 后：

```bash
cd ~/github/q2
source ~/github/esp-idf/export.sh
python3 q2.py build --target esp32s3 --config q2.esp32s3-am213.json --build-dir build/linux/esp32s3-am213
bash run.sh --config q2.esp32s3-am213.json --build-dir build/linux/esp32s3-am213 --port /dev/ttyUSB0
```

`build` 只编译；`run.sh` 默认编译/烧录/监视串口。已存在的构建目录可直接复用，旧诊断 Kconfig 不再参与代码选择。
AM178 使用 `q2.esp32s3.json`。两款配置均启用 display/apps，触摸主页不要求 input.keys；保留屏幕型号、亮度、旋转和字体配置。屏保和锁屏默认关闭，可在设置中开启非锁定屏保；没有实体按键 provider 时不能开启依赖按键解锁的锁屏。

目前实现显示、触摸、物理亮度 API、出厂 APP 管理、背景/系统 UI 的会话设置。
**Flash 私有存储、SD、Wi-Fi/HTTP、蓝牙、USB、音频、RTC/电池采样、实体按键仍未接入此平台**，不会注册虚假的可用能力。设置显示 Session only，重启回到 JSON 默认值。状态栏默认 Q2 / BAT --，不伪造电量或时钟。PC Music APP 还不能直接在此板联网播放。

## 工程边界

- `main/main.c`：唯一 Kernel/Lua/LVGL owner；注册 provider、绑定多 APP runtime、切换前台与故障返回。
- `main/embedded_apps.*`：只读固件 APP 目录、每 APP 图片注册、主页图标别名。保留 resident 图片，切换 APP 不会清掉其他实例资源。
- `scripts/embed_factory_apps.py`：消费显式 Q2-App 目录与产品 JSON，使用 Q2-App manifest 校验和 Lua 模块打包器；选择 factory.json 中 factory=true 的包（当前 Homepage/Settings）。只嵌入当前分辨率 PNG，缺少变体则采用默认 PNG。只授予产品与 manifest 共同声明的能力，缺少 required 授权会构建失败。
- `main/am178.*`：两个屏幕的共享驱动实现；名称保留，但硬件参数由 `panel_profile.h` 选择。
- `main/am178_vendor_init.h` / `am213_vendor_init.h`：厂家初始化序列；`am178_config.h` 统一 RGB565 和配置亮度。

资源生成位于构建目录 `esp-idf/main/factory/`，不手改。CMake 追踪 Lua、manifest、PNG、factory.json 和配置修改，直接重建即可更新资源；不需要另行复制到 SD 卡。当前分区仍是 15 MiB 单 factory APP，无 OTA/用户存储分区。

独立 Platform checkout 仍通过显式路径消费依赖：

```bash
idf.py -C esp32s3 -B /path/to/build -D Q2_KERNEL_DIR=/path/to/kernel \
  -D Q2_APP_DIR=/path/to/app -D Q2_APP_CONFIG=/path/to/product.json menuconfig
idf.py -C esp32s3 -B /path/to/build build
```

未注入 Q2_GENERATED_DIR 时，Kconfig 决定屏幕型号/方向/亮度；Q2_APP_CONFIG 的 app.startup、app.grants 与 display.width/height 用于应用和资源打包，尺寸必须与 Kconfig 一致。系统 UI 使用平台独立默认值。产品用户应优先使用根入口，将全部配置统一在根 JSON。

## 硬件和稳定驱动参数

参考模块为 N16R8（16 MiB Flash / 8 MiB 八线 PSRAM）。启动检查 PSRAM，GPIO 集中在 `main/board.h`。

GPIO 以 `main/board.h` 的当前用户接线为准，不照搬厂家 demo 的引脚。此次仅对齐显示参数、
驱动和刷新容量；具体引脚快照见 [AM213](docs/am213.md)。


AM213 原生 410×502、X gap 22、CST820；AM178 原生 368×448、X gap 16、CHSC6417。
0/180 度使用原生宽高，90/270 度交换宽高；像素和触摸做对应软件转换，禁止调用不支持的硬件 swap_xy/mirror。完整参数见 [AM213](docs/am213.md)。

AM213 对齐 OSPTEK LVGL9 无 TE 示例：官方 `esp_lcd_co5300` 2.0.3（manifest 固定版本、
`dependencies.lock` 固定依赖哈希），SPI2 mode 0、QSPI 40 MHz、32 位命令/8 位参数、队列深度 1、
RGB565 高字节先发、X gap 22、原厂寄存器序列。产品 JSON 默认亮度为 80%，初始化表与
post-display-on API 均由同一百分比驱动。驱动的亮度参数为百分比，不可传原始寄存器值。

ESP32-S3 使用 `q2_lvgl_full_refresh()` 完整画布模式：先完成 Lua 各 UI 层的合成，再旋转、
转换 RGB565 并同步发送原生行。AM213 绘图缓冲为 823280 字节，额外约 741 KiB PSRAM；
影子帧 411640 字节、内部 DMA 缓冲 65600 字节（80 行），每帧 7 个窗口、最后 22 行。
DMA 完成后才复用缓冲。PC 默认局部刷新；AM178 保留原有 10 MHz / 10 行 / 队列深度 10。
保留四方向旋转与触摸映射，未调用不支持的 swap_xy/mirror。TE 仍禁用；此处沿用 Q2 的
单 owner 调度，不引入厂家 adapter 的另一套 LVGL 任务。原厂带 TE 横屏示例也使用完整帧，
但本端没有启用它的 TE 握手，不等价于同步双缓冲；实屏残影和滚动效果仍需验证。
启动日志包含 `panel driver=esp_lcd_co5300/2.0.3` 与 `UI refresh=full-frame`。

触摸 INT 唤醒采集任务，I2C 重试/状态队列留在平台；每轮 owner 最多消费 16 个样本。没有新样本不代表松手，读取耗尽才报错并释放指针。GPIO46 仅输入；AM213 总线不启用内部 3.3 V 上拉，供电与电平转换由既有转接板处理。

正常日志保留启动摘要、亮度变更和错误；逐条初始化 HEX、首次触摸及成功重试统计降到 ESP-IDF DEBUG。Root logging 控制 Kernel 日志；ESP-IDF 日志级别通过 menuconfig 配置。SRE、COLMOD、启动地址按厂家表，亮度仍由产品配置统一控制。

## 自动验证与实机验收

```bash
python3 platform/esp32s3/tests/test_init.py
python3 platform/esp32s3/tests/test_panel_profiles.py
cmake -S platform/esp32s3/tests -B build/linux/embedded-apps -G Ninja \
  -DQ2_KERNEL_DIR="$PWD/kernel" -DQ2_APP_DIR="$PWD/app" \
  -DQ2_APP_CONFIG="$PWD/q2.esp32s3-am213.json"
cmake --build build/linux/embedded-apps -j 8
ctest --test-dir build/linux/embedded-apps --output-on-failure
```

集成测试实际运行固件生成的 Lua/PNG，在没有按键和存储 provider 下验证启动、反复触摸进入设置/返回、主页实例保留、空闲和清理。构建目录的 `partial/`、`full/` 生成主页/设置/滚动后 PPM 截图，并逐像素比较两种刷新模式。几何测试覆盖两种屏幕的四方向及触摸 IRQ/重试/释放，初始化测试覆盖亮度配置优先级。

此次通过 AM213 交叉编译和宿主运行验证；新的正式 UI 尚需用户烧录确认触摸、滚动、返回和长时间运行。之前屏幕实测成功不等于这一版整机功能均已验证。

## 来源

初始化命令和 CHSC6417 报告格式依据
[OSPTEK AM178 示例 ffe29d6746f1529e3b03041fd6887e1c653f4c33](https://github.com/osptek/amoled-1.78-368x448-qspi-co5300/tree/ffe29d6746f1529e3b03041fd6887e1c653f4c33)，
Copyright 2026 OSPTEK；仓库 MIT，资料标注 CC-BY-4.0。
改动：用户 GPIO、原生/软件旋转、同步 SPI、受限分块、Kernel 像素注册和 owner 触摸转发。
QSPI 命令/像素操作码参考 Espressif esp_lcd_co5300 2.2.0（Apache-2.0）协议。
原始资料保留在旧项目 third_party/osptek-am178 与 managed_components/espressif__esp_lcd_co5300，
当前面板组件来源及本地参考差异见 [驱动对照](docs/am178-vendor-alignment.md)。


