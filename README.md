# ReaGBA

REAPER 内的 GBA 模拟器：**原生扩展 + 系统 WebView 界面 + 原生 mGBA 核心**。核心在 REAPER 进程内运行，游戏像素由原生 GPU 渲染，音频由 SDL 输出；JavaScript 只处理界面和控制命令。

## 安装与打开

1. 在 ReaPack 中启用公开的 **zaibuyidao Scripts** 仓库，安装或更新 **ReaGBA**。
2. 完全退出并重新启动 REAPER，使原生扩展被加载。
3. 在操作列表搜索 **ReaGBA**，运行 **zaibuyidao: ReaGBA**。命令 ID 是 `_REAGBA_SHOW`，可绑定快捷键或工具栏。
4. 点击“打开 ROM”，选择自己的 `.gba` 文件。也可把游戏放进 REAPER 资源目录的 `Data/ReaGBA/ROM`，然后刷新游戏库。

入口由原生扩展注册，不需要 Lua 启动脚本或 ReaScript 依赖。ReaPack 将当前平台的扩展放入 `UserPlugins`，界面放入 `Scripts/zaibuyidao Scripts/ReaGBA/web`。`.ext` 是 ReaPack 的安装声明，不是用户执行的脚本。安装规则见 [ReaPack 官方打包文档](https://github.com/cfillion/reapack-index/wiki/Packaging-Documentation)。

获得独立平台安装 ZIP 的用户，也可退出 REAPER 后将其中的 `UserPlugins` 和 `Scripts` 合并到 REAPER 资源目录，再重新启动。

| 平台包 | 扩展文件 | 系统界面 |
| --- | --- | --- |
| windows-x64 | reaper_reagba-x64.dll | WebView2 Evergreen Runtime |
| macos-x86_64 | reaper_reagba-x86_64.dylib | WKWebView，macOS 12+ |
| macos-arm64 | reaper_reagba-arm64.dylib | WKWebView，macOS 12+ |
| linux-x86_64 | reaper_reagba-x86_64.so | WebKitGTK 4.1，X11/XWayland |
| linux-aarch64 | reaper_reagba-aarch64.so | WebKitGTK 4.1，X11/XWayland |

Linux 的 `extension/reagba-webview-*` 是界面与原生 OpenGL 渲染辅助进程，用来隔离 REAPER 与 WebKitGTK 的 GDK 版本。GBA 核心、音频、存档仍在扩展中；辅助进程通过共享内存接收帧，像素不经过 JavaScript。原生 Wayland 停靠尚不支持。Ubuntu 24.04 的运行依赖为 `libwebkit2gtk-4.1-0 libepoxy0 libasound2t64`。

正式安装包不包含 `ReaGBA.exe`、验证程序、ROM、BIOS 或用户数据。macOS 包尚未签名或公证。

## 操作与布局

默认键盘：**W/S/A/D = 上/下/左/右，J/K = A/B，Q/E = L/R，回车 = Start，空格 = Select**。**按住 R 临时 4× 加速，松开恢复原倍率**。点击游戏画面获得输入焦点；编辑搜索或键位设置时会阻止游戏输入，离开 ReaGBA 后释放按键。设置中可修改或恢复默认键位，也支持 SDL GameController 兼容手柄。

右上角“停靠 / 取消停靠”切换 REAPER Docker 与浮动窗口，切换时保留正在运行的核心。界面只采用上下布局：游戏库在上，游戏画面在开始/暂停按钮上方。画面区域保持 GBA 的 3:2 比例；额外高度用于游戏库。拖动中间分隔条可改变占比，双击恢复自动分配。整数缩放可能产生少量黑边，可在设置中关闭。

支持暂停、继续、重置、停止、1×/2×/4× 倍率、音量、跳帧、9 个即时存档槽、电池存档、BMP 截图、收藏、搜索和最近游玩。只支持直接加载 `.gba`，ZIP 需先解压。

新安装的数据位于 REAPER 资源目录的 `Data/ReaGBA`，与 ReaPack 管理的界面分开。如果没有新数据目录而检测到旧 `Scripts/zaibuyidao Scripts/Various/ReaGBA/data`，继续使用旧进度；旧 ROM 目录也会被识别，不移动游戏文件。电池存档在暂停、切换游戏、正常关闭及定时检查时保存；即时存档按 ROM SHA-256 和核心版本核对。

## 构建与验证

需要 Git、Python 3、CMake 3.24+ 与 C++17 编译器。Windows 使用 Visual Studio 2022 C++ 工具链；macOS 使用 Xcode Command Line Tools。依赖自动获取到忽略提交的 `third_party` 或构建目录。

```sh
python scripts/build.py
```

该命令构建扩展、运行 CTest，并把可安装文件放入 `build/native/package`。仅运行核心检查可配置 `-DREAGBA_EXTENSION=OFF`。验证工具的可执行文件只用于开发，不进入安装包。

Linux 开发依赖：

```sh
sudo apt-get install build-essential cmake ninja-build git python3 pkg-config libx11-dev libgtk-3-dev libwebkit2gtk-4.1-dev libepoxy-dev libasound2-dev libudev-dev
```

手动构建和打包：

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build/native --config Release --parallel 2
ctest --test-dir build/native -C Release --output-on-failure
cmake --install build/native --config Release --component ReaGBA --prefix build/package
python scripts/release.py --stage build/package --platform windows-x64 --output dist
```

最后一条的 platform 按目标系统替换。macOS 配置时增加 `-DCMAKE_OSX_ARCHITECTURES=arm64` 或 `x86_64`、`-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`。

实际 ROM 验证需自行提供文件：

```powershell
./build/native/bin/Release/reagba_verify.exe ./ROM/game.gba ./verification/core 7200
./build/native/bin/Release/reagba_input_verify.exe ./ROM/game.gba ./verification/keys.json
python scripts/extension_smoke.py --reaper C:/REAPER/reaper.exe --rom ./ROM/game.gba
```

扩展测试使用独立的 `build/extension-smoke` REAPER 配置和 `verification` 存档，不操作日常工程。无 ROM 的 CTest 覆盖核心边界、Windows 键位、WebView 通信、发布包边界和 Linux 共享帧恢复。可选的布局测试需要 Playwright：`npm install --no-save playwright`、`npx playwright install chromium` 后运行 `node tests/layout_verify.cjs`。

Windows 本地已验证 REAPER 7.78 中注册操作、塞尔达 ROM 运行、音频设备、停靠/取消停靠及存读档。macOS 和 Linux 的桌面停靠、音频设备、实体手柄仍需对应系统实机验收；GitHub Actions 编译成功不能替代这些检查。详细验证范围见 [docs/VALIDATION.md](docs/VALIDATION.md)。

## 自动构建

私有源码仓库 `reagba` 独立构建，编辑 `ui/index.html`、`ui/style.css`、`ui/app.js` 后会自动合成发布用的 `web/index.html`。GitHub Actions 为五个平台编译、测试，生成安装 ZIP、原生扩展文件和 **ReaGBA-ReaPack-vX.Y.Z.zip**。该发布包只有 `ReaGBA/extension`、`ReaGBA/web`、`ReaGBA/ReaGBA.ext`。

维护者自行从私有 Release 下载，将发布包中的 ReaGBA 目录整理进公开的 `ReaScripts/ReaGBA`，再提交并更新 ReaPack 索引。`.ext` 的所有下载地址只指向公开的 ReaScripts 仓库；运行时不需要私有源码仓库、构建工具或 GitHub 认证。工作流不会修改 ReaScripts，也不会自动跨仓库发布。

推送 main 时，工作流读取 CMake 版本号，首次出现该版本才创建 Release；已发布版本不会覆盖。提交新版本前修改 `CMakeLists.txt` 的版本号。普通分支及 PR 只产生构建产物。详见 [docs/RELEASING.md](docs/RELEASING.md)。

架构见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，依赖来源与许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
