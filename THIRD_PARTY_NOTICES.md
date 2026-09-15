# Third-party source and linking inventory

ReaGBA 使用以下上游依赖。ROM、Nintendo BIOS 和用户进度不在源码仓库或安装包中；应用未修改 mGBA 的上游源码。

| 依赖 | 构建固定版本 / commit | 使用与许可 |
| --- | --- | --- |
| [mGBA](https://github.com/mgba-emu/mgba/tree/0.10.5) | 0.10.5 / 26b7884bc25a5933960f3cdcd98bac1ae14d42e2 | GBA 核心，静态链接；MPL-2.0，内部组件另有许可 |
| blip_buf（随 mGBA） | mGBA 0.10.5 中的 1.1.0 | 音频重采样，静态链接；LGPL-2.1-or-later |
| inih（随 mGBA） | 同上 | 配置解析；BSD 风格许可 |
| [SDL](https://github.com/libsdl-org/SDL/tree/release-2.32.8) | release-2.32.8 / 98d1f3a45aae568ccd6ed5fec179330f47d4d356 | 音频、手柄、物理键码表；静态链接；zlib，hidapi 内含 BSD 许可 |
| [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0) | v3.12.0 / 55f93686c01528224f448c19128836e7df245f72 | header-only；MIT |
| [REAPER SDK](https://github.com/justinfrankel/reaper-sdk/tree/490ded57668727fba21482fabc50ba9853a457bb) | 490ded57668727fba21482fabc50ba9853a457bb | REAPER 插件接口；zlib 风格许可 |
| [WDL / SWELL](https://github.com/justinfrankel/WDL/tree/cc8eaf178ae871b96cd316a27d84e6be34540636) | cc8eaf178ae871b96cd316a27d84e6be34540636 | macOS/Linux 窗口接口与 modstub；zlib 风格许可 |
| [Microsoft WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.1150.38) | 1.0.1150.38 | Windows 头文件及静态 Loader；Microsoft 许可，系统 Runtime 不打包 |
| Apple WKWebView / OpenGL | 系统框架 | 系统动态链接，不打包 |
| GTK3 / WebKitGTK 4.1 / libepoxy / X11 | Linux 系统库 | 动态链接，不打包；各组件保留其上游许可 |

对应许可证文本位于 licenses。mGBA 的 MPL 文件、blip_buf 的 LGPL 文件及其余依赖以各自许可证为准，应用源码不会更改这些授权。完整应用构建脚本与固定依赖源码入口随仓库提供，可用于修改依赖并重新链接。依赖下载在 third_party 或构建目录内进行，既有目录优先用于本地开发；干净 CI 从上述固定版本获取。

REAPER 宿主实现和发布流程参考同一作者的 [Sendmanager](https://github.com/zaibuyidao/Sendmanager)。应用自身的发布授权由仓库所有者另行声明。

## 内置 LCD 效果

- LCD3X 采用 Gigaherz 的 [LCD3x 正弦遮罩](https://github.com/libretro/glsl-shaders/blob/master/handheld/shaders/lcd3x.glsl)，上游声明为 Public domain；采用默认亮度参数 16 / 4。
- lcd-grid-v2 参考 cgwg 的 [LCD 子像素积分模型](https://github.com/libretro/glsl-shaders/blob/master/handheld/shaders/lcd-cgwg/lcd-grid-v2.glsl)，在 `src/video/BuiltinShaders.h` 中实现积分多项式的 Horner 求值和 D3D/OpenGL 公用公式；默认 RGB 原色、gamma 3 / 2.2、black level 0.05。未打包上游完整 shader 文件或 RetroArch 加载器。
