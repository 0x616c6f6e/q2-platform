# AM213 厂商配置对照（2026-10-12）

参考 [OSPTEK 08e5d097](https://github.com/osptek/amoled-2.13-410x502-qspi-co5300/tree/08e5d097dcb5b9768b52f6732ad22435b2f8456e/versions/AM213Q410502LK/examples/esp32s3-idf5_co5300-qspi_esp-lvgl-adapter_lvgl9)。
对应 `main/main.c`、`co5300_init_cmds.h` 与 `idf_component.yml`；Copyright 2026 OSPTEK / CC-BY-4.0。
本次参考快照下载在根 build/vendor-am213-reference（生成参考资料，不参与构建）。

| 项目 | 厂商 LVGL9 示例 | Q2 AM213 |
| --- | --- | --- |
| Panel 驱动 | esp_lcd_co5300 2.0.* | 固定 2.0.3，SDK component manager 管理 |
| QSPI | SPI2、mode 0、40 MHz、cmd32/param8、queue1 | 一致 |
| SPI 最大传输 | 410×80×2 字节 | 一致，内部 DMA tile 同样为 80 行 |
| 显存尺寸/偏移 | 410×502，(22,0) | 一致；逻辑横屏 502×410 在发送前软件旋转 |
| Panel 像素格式 | RGB565，3A=55、RGB 顺序 | 一致；Kernel 内部 XRGB8888 转 RGB565 |
| 初始化 | FE00,11,FE00,C480,3A55,5320,63FF,2A,2B,29,51,5807 | 除亮度来源统一外，命令/数据/延时一致 |
| 最终亮度 | API 80%，先在表中写 FF | JSON 80%，表与 API 都用 CC，避免双重配置 |
| 对齐 | 起点偶数、终点奇数 | 原生窗口按相同规则，完整帧/每块均满足 |
| GPIO | 示例开发板接线 | 保留 main/board.h 用户接线 |
| LVGL 接入 | esp_lvgl_adapter 0.4.* + LVGL9.4.* 独立任务 | 保留 Kernel 的 LVGL9.3 单 owner、完整帧刷新 |
| TE | 禁用 | 禁用 |
| 触摸 | CST820 400 kHz、示例板内部上拉 | 保留现有 IRQ 队列/100 kHz/转接板外部上拉 |

厂家另有 `with-te-sw-rotate-90` 示例，启用 full_refresh 并在发送阶段旋转。本端采用同样的
完整帧思路，未拷贝其 LVGL8 ABI、TE 任务或双缓冲实现。不能将“参数对齐”描述为整个 demo 原样移植。

实测前证据：AM213 ESP-IDF 交叉编译；两款屏幕四方向、实际 flush 非纯色/步长/奇数区域测试；
主页与设置真实 Lua/PNG 集成测试及完整/局部绘制逐像素对比。未连接串口烧录，持续残影是否消除待实机确认。
