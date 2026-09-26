# 核心构建与发布

| 仓库 | 职责 |
| --- | --- |
| reagba | GBA 核心、Native Service / Frame Stream 接入、`ui/` 界面、测试和独立发布流程 |
| ReaWebAPI | 通用 WebView、Native Service、Native Stream 与平台能力 |
| ReaScripts/ReaGBA | 维护者手动整理的公开 ReaPack 核心包 |

ReaGBA 的 CMake 安装组件只安装 `UserPlugins/reaper_reagba-<arch>.*`。Windows 构建文件可放入 `C:\REAPER\UserPlugins`，重启 REAPER 后加载。UI 和 Lua 启动器在本仓库 `ui/` 独立维护、分发。运行时不依赖核心源码检出目录。

`.github/workflows/build-native.yml` 继续构建 Windows x64、macOS x86_64/arm64、Linux x86_64/aarch64。平台 ZIP 只含核心二进制和 `ReaGBA-core.json` 校验清单。汇总产物包含五个平台 ZIP、五个核心二进制、ReaPack 核心包及 SHA256SUMS，不包含 WebView 辅助程序或 Web 文件。

ReaPack 包内为 `ReaGBA/ReaGBA.ext` 与 `ReaGBA/extension/` 下的五个核心文件，描述文件不注册 Action，也不安装 Lua/UI。用户通过单独提供的 `ui/Open.lua` 启动界面。

发布前由维护者递增 CMake 版本号。现有不可变 Release 不会被覆盖。工作流只发布本仓库的产物，不修改 ReaScripts 或 ReaWebAPI，也不会自动提交跨仓库修改。

```sh
cmake --install build/native --config Release --component ReaGBA --prefix build/core-package
python scripts/release.py --stage build/core-package --platform windows-x64 --output dist
```

安装及发布收集采用显式二进制白名单，不要将整个构建目录打包。ROM、BIOS、存档、截图、测试输出和第三方构建目录不进入发行包。
