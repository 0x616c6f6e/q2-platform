# AM178 A0 / FF 发光面积对照

目的：复现“全屏 A0 正常、FF 异常”，检查相同颜色数据在小面积时是否正常。
本测试不自动判断通过，也不把 DMA 完成当作屏幕显示正确。

## 运行

在 `/home/alon/github/q2` 激活环境：

```sh
source ~/github/esp-idf/export.sh
bash menuconfig.sh
# Q2 AM178 display diagnosis → Startup mode → Black-background area test
bash build.sh
bash run.sh --port /dev/ttyACM0
```

串口名替换为实际设备。面积测试需要在当前构建中选择；干净构建默认是 Lua display demo。恢复 Lua 时在同一菜单选择该项，再编译烧录。
无需修改 APP、项目分辨率或源代码。另保留 Direct C full-screen A0 RGB cycle 作为对照。

## 流程

测试期间保持项目启动亮度不变：由 `q2.esp32s3.json` 的 `display.brightness` 统一设置。
当前为 100%（0x51=0xFF），0x63=0x80、0x58=07，QSPI 10 MHz。
记录结果时请附带配置亮度；对比其他亮度时只改该 JSON 字段。
不启动 Kernel、Lua、LVGL、触摸、状态栏或屏保，不叠加文字，避免额外发光负载。

面积依次约为 **1%、5%、10%、25%、50%、75%、100%**；每个面积依次为：

1. RED A0 → RED FF
2. GREEN A0 → GREEN FF
3. BLUE A0 → BLUE FF

共 42 步。矩形居中且保持接近屏幕的长宽比，坐标/尺寸取偶数；串口打印实际面积比例。
每一步先整屏黑色保持 **1 秒**，再显示色块 **3 秒**（从 DMA 完成后计时）。
正常的 1 秒黑色间隔不是故障。全过程约 3 分钟，结束停留在全屏 A0 蓝色，重启重新测试。
如果屏幕已失去响应，结尾 A0 也不保证恢复；必须根据日志及实际显示记录结果。

黑帧和彩帧都逐行发送完整屏幕，包括色块外的黑色像素。每帧分块和像素数量相同，
与既有 LVGL 32 行逻辑分块一致，并经过相同 RGB565 转换及内部 DMA 通路。
因此面积变化不同时改变传输字节数；矩形位置附近的数据比特分布仍会变化。

## 观察记录

- `AREA_STEP`：即将开始的步骤、颜色值、矩形坐标/尺寸、实际面积。之后先有黑色间隔。
- `AREA_HOLD`：彩帧 DMA 已结束，从此观察 3 秒；应看到对应颜色与面积。
- `AREA_TEST complete`：发送完全部步骤，停留 A0 蓝色；不是“屏幕测试通过”。
- `AREA_TEST stopped`：软件检测到错误，会打印步骤/阶段及返回值。

记录第一次异常的 step，以及是黑屏、保留上一帧、缺色还是局部刷新；记录后续 A0 是否恢复。
最好拍摄屏幕与串口同时可见的视频，避免把黑色间隔与异常混淆。

| 观察结果 | 支持的下一步排查方向 |
|---|---|
| 小面积 FF 正常，面积变大后 FF 异常，而同面积 A0 正常 | 优先测屏端供电跌落/纹波及驱动电源状态；仍不能单凭此认定供电不足 |
| 最小面积 FF 就异常，A0 正常 | 比较数据比特相关的 QSPI 信号、像素格式及面板寄存器状态 |
| 独立面积测试稳定，Lua 同条件仍异常 | 比较 UI 更新节奏、窗口和缓冲同步；避免归因于 Lua 语言本身 |
| A0 和 FF 均异常 | 记录首次阶段，重新核对共同的初始化、连接和供电条件 |

A0 是像素通道值；display.brightness 是全局亮度寄存器百分比，两者不能换算成准确的电流或实测亮度。
没有电压/波形测量，本实验只能缩小原因范围。

## 软件验证

在 q2 根目录：

```sh
mkdir -p build/area-test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ikernel/include -Iplatform/esp32s3/main \
  platform/esp32s3/main/am178_area_test.c platform/esp32s3/tests/test_area.c \
  -o build/area-test/test_area
build/area-test/test_area
python3 kernel/scripts/check_contracts.py
```

主机测试验证图案内容、全帧覆盖、A0/FF 同面积、两种方向和错误传播；
ESP32-S3 编译验证 SDK 接入。上述验证不替代实机观察。
