# AM178 数据模式诊断 V7

当前保留用户配置：9 个色值、黑色底图、1 档 10 MHz、100% 亮度（产品配置）。
颜色在 main/am178_blue_pattern.c；数量、背景、时钟组数在 main/am178_blue_test.h。
将 CLOCK_GROUPS 改为 2 才会增加 2 MHz；编译时检查色表数量，日志打印实际值。
每个测试复位初始化，底图 2 秒 → 中央色块 4 秒 → 无复位 A0 蓝色恢复 2 秒。
不运行 Kernel/Lua/LVGL/触摸。结束后保持恢复色；完成日志不代表视觉通过。

## 单笔事务发送

新增 CONFIG_Q2_AM178_SINGLE_TRANSACTION：BLUE_TEST 模式默认启用，其他启动模式默认关闭。
硬件 command=8 位、address=24 位、数据 QIO，一笔 SPI 事务发送头与像素；不经过
esp_lcd 的两笔头/数据交接。仍使用 DMA；同步收集 SPI 最终结果并传递错误。
初始化寄存器、颜色格式、亮度、引脚、旋转、测试色表不因传输开关改变。
原 ESP-LCD 模式保留，可在 menuconfig 的 Q2 AM178 display diagnosis 中关闭此选项对照。

```bash
cd /home/alon/github/q2
bash build.sh
bash run.sh --port /dev/ttyACM0
```

新日志应出现 `BLUE_TEST_V7` 和 `TRANSPORT single cmd=8 addr=24 data=QIO DMA=enabled`。
旧模式显示 `TRANSPORT esp-lcd split-header-data`。

## 修复和验证

厂商 draw_bitmap 现在返回 tx_color 的错误；不再以 ESP_OK 掩盖发送失败。
测试日志不再硬编码旧红底、旧色表和 2 MHz。颜色数量不匹配会导致编译失败。
主机已验证像素打包、窗口、旋转、错误回传及 SPI 事务字段；新路径尚需实机验证。
原始审查在 am178-transfer-audit.md；修复后的状态以本页为准。
