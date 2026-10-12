# Hardware Lab 本机硬件模拟后台

提供圆盘按键、电池、Wi-Fi 与 BLE 数据模拟，不含屏幕预览与触摸接口。页面嵌入可执行文件，无需 Node、Python Web 服务、CDN 或额外运行库。

## 启动与操作

```powershell
./run.ps1 -AppName key-demo
```

在 q2 根目录运行，打开 http://127.0.0.1:8765/。圆盘上方 POWER、下方 BACK、左侧 PREV、右侧 NEXT、中央 PLAY。它模拟五个独立按键，不是连续滑环输入。鼠标/触屏轻点、双击或按住；也可 Tab 聚焦后用空格或 Enter，键盘自动重复不会产生重复按下。

默认注册 POWER、BACK、PREV、NEXT、PLAY。修改 q2.json 的 `keys.names` 替换注册表，其他名称显示在圆盘下方。`web.port` 指定端口，0 自动分配，`-DisableWeb` 关闭后台。原生 exe 需要 `--web-port PORT` 才监听。端口占用会报错，不接管其他进程。

Key Demo 需要 display.ui + input.keys；system.log 可选。默认脚本提供 grants，APP 仍只能获得 manifest 声明的交集。APP 自行 watch(true) 订阅。

## 设备数据调试

启动默认 Homepage 或任意订阅数据的 APP，打开页面下方的“设备数据”。编辑后点击每组的“应用模拟”，不会在每次输入时发送半成品数值；轮询不会覆盖尚未提交的输入。

| 分组 | 可输入数据 | 发布到 Lua 的 key |
|---|---|---|
| 电池 | 电量 0–100%、电压 0–20000 mV、充电开关 | power.battery.percent / voltage_mv / charging |
| Wi-Fi | 连接开关、RSSI -127–0 dBm | wireless.wifi.connected / rssi_dbm |
| BLE | 连接开关、RSSI -127–0 dBm | wireless.ble.connected / rssi_dbm |

每组可选择正常值、模拟读取失败（io_error）或模拟过期（timeout）；“停用模拟”恢复 unsupported。无线连接断开时 connected 返回 false，RSSI 返回 invalid_state。BLE 与经典蓝牙 wireless.bt.connected 独立，不会改变后者。电压输入范围是调试协议范围，不是本机电池的安全工作电压。

需要 q2.json 的 components.data=true。数值由 x86 owner 发布到 Kernel data，APP 使用 q.data.get/subscribe，权限为 data.read 加 power.read 或 wireless.read；不是只修改网页文字。模拟值保持到再次修改、停用或进程退出；重启默认全部停用，不操作电脑真实无线设备。

Homepage 默认状态栏包含 `{ble}`；旧用户偏好会保留，可在系统设置的状态栏内容中选择新版完整模板。状态栏目前显示连接标记与电量，RSSI、电压供 APP 读取，不会自动绘制信号格。

## 统一时序配置

编辑 **q2/q2.json**，然后重新运行 run.ps1 编译。页面从设备状态读取当前已编译的阈值，无第二份 JavaScript 时间参数。

| 配置 | 默认值 | 含义 |
|---|---|---|
| keys.single_min_ms | 20 ms | 最短有效按下时间，小于它的脉冲不产生点击 |
| keys.double_gap_ms | 300 ms | 第一次松开到第二次按下的最大间隔，不含边界；也是单击确认等待时间 |
| keys.long_press_ms | 800 ms | 持续按下达到此值立即上报一次长按 |

短按的持续时间为 `[SINGLE_MIN, LONG_PRESS)`。第一次松开后等待双击窗口：没有第二次按下才发 SINGLE；窗口内第二次按下并短按松开，发一次 DOUBLE，不另发 SINGLE。第二次按下恰好在窗口末端，按两个单击处理。连续三次短按是 DOUBLE 然后 SINGLE。

长按只触发一次，松开不再发单击。如果第一次短按、第二次持续长按，则在第二次长按成立时发第一次 SINGLE 和第二次 LONG。无效的短脉冲不吞掉已有的第一次单击。

x86 的 key_input.c 为每个键维护独立状态，使用 GetTickCount64 的单调时间，以服务器收到边沿的时间为准。网络/owner 调度会造成毫秒级误差，不等于实物 GPIO 消抖验证。后续 ESP 驱动应采用同一 SoC 配置策略，当前尚未接入物理按键。

## 分层和异常处理

浏览器只发 down/up/cancel，x86 owner 识别手势，调用 Kernel q2_keys_report。Kernel 的 key.action 仍是名称 + SINGLE/DOUBLE/LONG（1/2/3），不处理识别或业务操作；Lua APP API 无变化。

页面失焦、隐藏、指针取消或 Escape 取消当前按住的输入，不生成点击。已经松开的有效短按仍等待识别。保持按下时网页每 250 ms 续约；浏览器丢失 2 秒后 x86 取消仍按住的键，防止卡键。失联前已经成立的长按不能撤销。多个页面共用一个模拟器输入状态，建议一次使用一个控制页面。

识别记录来自服务器最近 16 个事件，页面保留最多 30 条，不把“边沿接收成功”误认为“已经识别”。reports 为尝试上报的总数，不是 APP 消费数。异步报告失败也写入记录及设备日志；队列满不自动重放。

## HTTP 接口

服务器仅绑定 127.0.0.1，校验 Host/Origin，POST 需随机 X-Q2-Token，不开放 CORS。最多 4 个连接，请求头最多 4096 字节、响应最多 64 KiB，连接超时 3 秒。它是本机开发工具。

| 路径 | 用途 |
|---|---|
| GET / | 内嵌圆盘页面 |
| GET /api/state | screensaver/content_height/status_bar_height、注册名称、按住状态、最近 action、timing、事件历史、报告次数、simulation 与 token |
| POST /api/key?name=POWER&state=down | 按下，state 还支持 up / cancel；无请求体 |
| POST /api/device?group=battery&mode=value&percent=42&voltage_mv=3800&charging=1 | 设置电池模拟值；布尔值只能为 0/1 |
| POST /api/device?group=wifi&mode=value&connected=1&rssi_dbm=-65 | 设置 Wi-Fi；group=ble 设置 BLE |
| POST /api/device?group=ble&mode=off | 停用；mode=error/timeout 可模拟异常，无其他参数 |
| POST /api/heartbeat | 续约按住的输入，不产生按键事件 |

设备参数必须完整且在范围内，不接受重复或未知字段；非法请求 HTTP 400 且不更改模拟配置。此端点同样校验 Host/Origin/token。GET /api/state 的 simulation 展示模拟器配置，APP 的有效数据以 data API 为准。

旧 action=single/double/long 直接注入接口已移除。成功只表示本次输入被接受；识别之后的投递结果看 events.result（0 为成功），APP 实际响应看 Key Demo。无订阅者仅更新 Kernel 最近 action，事件不重放。

## 验证

```powershell
ctest --test-dir build/x86 --output-on-failure
python platform/x86/test_web.py --runner build/x86/bin/q2_x86.exe --app-root app
python platform/x86/test_web_data.py --runner build/x86/bin/q2_x86.exe
```

test_key_input.c 用假时钟验证精确边界、独立按键、重复电平、取消、投递失败不重放；test_web.py 使用真实 x86、随机端口和 Lua，验证从边沿到事件的完整链路以及 HTTP 拒绝、超时释放、无屏幕/触摸端点。浏览器预览：[圆盘页面](../../../docs/images/key-lab.png)。

浏览器交互回归可用 Node 22+ 与 Chrome 执行 `test_web_ui.mjs`，命令见 [验证记录](../../../docs/validation/key-lab.md)。它在隔离 profile 中真正操作圆盘，不注入已经识别的事件。

## 屏保唤醒

有系统 UI 时，原始按下先报告用户活动。首次唤醒手势的 SINGLE/DOUBLE/LONG 全部被吞掉，reports/events/Kernel 最近动作均不增加；后续独立手势恢复正常。持续按住保持活动，cancel/掉线清除相关输入状态。Kernel 不解释按键业务。真实 30 秒超时回归：`python platform/x86/test_system_ui.py --runner build/x86/bin/q2_x86.exe --app-root app`。


管理模式下 key.action 先投递 resident lifecycle，再由它决定是否转给前台。锁屏中的 reports/logs 表示驱动识别到了动作，不代表普通 APP 收到；双击 POWER 解锁由 Home 消耗。/api/state 的 lifecycle/locked/screensaver 可辅助检查。非锁定屏保首次手势仍只唤醒。后台不提供屏幕预览或触摸接口。

设备数据测试覆盖 HTTP → Kernel → 真实 Lua get/订阅，含 0/false、范围与整数溢出、连接断开、错误/过期、TTL 保活和 BLE/BT 隔离。[验证记录](../../../docs/validation/web-device-data.md) · [当前页面](../../../docs/images/hardware-lab.png)。
