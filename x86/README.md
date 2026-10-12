# x86 后端

Windows 原生窗口 / Linux SDL2、输入和 Web 模拟器实现目录。产品入口已经移动到 q2 根目录；参见 [PC 使用说明](../../docs/x86.md)。

独立构建仍可执行 `cmake -S . -B ../build/x86 -DQ2_KERNEL_DIR=/path/to/kernel`，再 `cmake --build ../build/x86`。运行 exe 需要两个初始位置参数 manifest.json / bundled-main.lua 和明确 --grant；缺少参数会显示帮助。

[Linux SDL2](../../docs/linux-x86.md) · [Web 协议](web/README.md) · [Platform 独立构建](../README.md)

## 按分辨率选择 APP 图标

资源加载器优先选 `assets/WIDTHxHEIGHT/name.png`，缺失时回退 `assets/name.png`。尺寸取自 q2_lvgl_resolution，与系统状态栏无关；APP 继续使用平铺的逻辑 key，Homepage catalog 别名也按相同规则解析。只加载当前目录与默认资源的并集，不载入其他分辨率；匹配的坏文件报错，资源文件/目录重解析点均拒绝。
