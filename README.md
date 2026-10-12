# Q2-Platform

原 Q2-SoC 现更名为 Q2-Platform。独立 Git 仓库，拥有平台时钟、内存、SDK、窗口、输入与设备适配；Kernel 不包含 SDK。通过显式 Q2_KERNEL_DIR 消费 Kernel。

| 后端 | 当前能力 |
|---|---|
| x86 Windows | 原生窗口、LVGL、按键/Web 模拟、WinHTTP、音频、文件 LittleFS、可选 SQLite |
| x86 Linux | SDL2 窗口、LVGL、按键/Web 模拟、POSIX 文件 LittleFS、可选 SQLite、libcurl HTTP/HTTPS；无音频 |
| host | 控制台文本显示、RAM LittleFS、Lua 单 APP、可选内存 SQLite |
| esp32s3 | [AM178 显示/触摸、LVGL 中文与 Lua 验证入口](esp32s3/README.md)，N16R8 参考配置；其他设备能力未迁移 |
| esp32s31 | ESP-IDF 最小 Kernel/runtime 入口，尚无产品外设 |

产品构建、启动和配置在父级 q2 根目录：`build.ps1`、`run.ps1`、`q2.py`、`q2.json`。见 [产品文档](../README.md)、[接入规范](../docs/platform-development.md)、[配置规则](../docs/configuration.md)。这些父级文档不参与独立 Platform 编译。

## 独立构建

```sh
cmake -S x86 -B build/x86 -G Ninja -DQ2_KERNEL_DIR=/path/to/kernel
cmake --build build/x86
ctest --test-dir build/x86 --output-on-failure
```

Zig 增加 `-DCMAKE_C_COMPILER=/path/to/zig.exe -DCMAKE_C_COMPILER_ARG1=cc`。输出 build/x86/q2_x86.exe，需要显式提供已打包 APP 的 manifest/source 和 grants。host 以同样方式配置，-S 改 host。

common/q2_platform_config.h 提供独立后端默认值；完整产品通过 Q2_GENERATED_DIR 显式注入生成头，不寻找隐含兄弟源码或配置。不要将产品默认值再抄回本仓库另建配置文件。

ESP 使用激活的 SDK：`idf.py -C esp32s31 -D Q2_KERNEL_DIR=/path/to/kernel build`。只编译，不烧录；日志可由独立 menuconfig 配置，完整产品构建时以根 q2.json 为准。

所有 Q2 API 在 owner 调用；ISR/其他线程先排队。按键时序在 x86/key_input.c，Kernel 仅透传识别结果，APP 实现业务逻辑。未实现后端不能伪装为 READY。

Windows x86 现提供 MP3/WAV 音频后端（`audio_win32.c`）：工作线程下载临时文件并通过默认音频设备播放，
支持暂停、继续、真实进度、完成状态与取消。接口与限制见 [audio](../kernel/docs/audio.md)。
产品开关为根目录 q2.json 的 `components.audio`；ESP32/I2S 音频移植尚未实现。
