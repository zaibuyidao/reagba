# 核心构建与发布

| 仓库 | 职责 |
| --- | --- |
| reagba | GBA / GB / GBC 核心、Native Service / Frame Stream 接入、`web/` 界面、测试和独立发布流程 |
| ReaWebAPI | 通用 WebView、Native Service、Native Stream 与平台能力 |
| ReaScripts/Modules/ReaGBA | 维护者发布的 ReaPack 扩展与界面资源 |

ReaGBA 的 CMake 安装组件只安装 `UserPlugins/reaper_reagba-<arch>.*`。Windows 构建文件可放入 `C:\REAPER\UserPlugins`，重启 REAPER 后加载。界面和 Lua 启动器在本仓库 `web/` 维护，随 ReaPack 包分发。运行时不依赖核心源码检出目录。

`.github/workflows/build-native.yml` 构建 Windows x64、macOS x86_64/arm64、Linux x86_64/aarch64。平台 ZIP 只含核心二进制和 `ReaGBA-core.json` 校验清单。汇总产物包含五个平台 ZIP、五个核心二进制、ReaPack 包及 SHA256SUMS，不包含 WebView 辅助程序。

`ReaGBA-ReaPack-v<version>.zip` 包内的 `ReaGBA/` 包含 `extension/` 下的五个核心文件、`web/` 下的界面和启动文件，以及 `ReaGBA.ext`。发布到 ReaScripts 时将此目录放入 `Modules/`。描述文件安装核心与 Web 文件，Lua 启动器标记为 `nomain`，用户手动加载 `web/zaibuyidao_ReaGBA.lua` 启动界面。`@link` 指向 [REAPER 论坛主题](https://forum.cockos.com/showthread.php?t=311202)。

发布前递增 `CMakeLists.txt` 中的 ReaGBA 版本号，打包时 `ReaGBA.ext` 的 `@version` 自动读取该版本，无需单独维护。用本次版本的实际改动替换 `scripts/reapack.py` 中的 `CHANGELOG`。GitHub Release 正文与 `ReaGBA.ext` 的 `@changelog` 共用这些条目，不自动追加历史内容或 Full Changelog 链接。现有不可变 Release 不会被覆盖。工作流只发布本仓库的产物，不修改 ReaScripts 或 ReaWebAPI，也不会自动提交跨仓库修改。

```sh
cmake --install build/native --config Release --component ReaGBA --prefix build/core-package
python scripts/release.py --stage build/core-package --platform windows-x64 --output dist
```

安装及发布收集采用显式二进制和 Web 文件白名单，不要将整个构建目录打包。ROM、BIOS、存档、截图、测试输出和第三方构建目录不进入发行包。
