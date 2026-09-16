# ReaGBA

REAPER 内的 GBA 模拟器：**原生扩展 + 系统 WebView 界面 + 原生 mGBA 核心**。核心在 REAPER 进程内运行，游戏像素由原生 GPU 渲染，音频由 SDL 输出；JavaScript 只处理界面和控制命令。

## 安装与打开

1. 在 ReaPack 中启用公开的 **zaibuyidao Scripts** 仓库，安装或更新 **ReaGBA**。
2. 完全退出并重新启动 REAPER，使原生扩展被加载。
3. 在操作列表搜索 **ReaGBA**，运行 **zaibuyidao: ReaGBA**。命令 ID 是 `_REAGBA_SHOW`，可绑定快捷键或工具栏。
4. 点击“打开 ROM”，选择自己的 `.gba` 文件；文件选择器会记住上次打开的位置。也可在设置中选择 ROM 游戏文件夹，默认是 `Scripts/zaibuyidao Scripts/ReaGBA/ROM`。

入口由原生扩展注册，不需要 Lua 启动脚本或 ReaScript 依赖。ReaPack 将当前平台的扩展放入 `UserPlugins`，界面放入 `Scripts/zaibuyidao Scripts/ReaGBA/web`。`.ext` 是 ReaPack 的安装声明，不是用户执行的脚本。安装规则见 [ReaPack 官方打包文档](https://github.com/cfillion/reapack-index/wiki/Packaging-Documentation)。

获得独立平台安装 ZIP 的用户，也可退出 REAPER 后将其中的 `UserPlugins` 和 `Scripts` 合并到 REAPER 资源目录，再重新启动。

| 平台包 | 扩展文件 | 系统界面 |
| --- | --- | --- |
| windows-x64 | reaper_reagba-x64.dll | WebView2 Evergreen Runtime |
| macos-x86_64 | reaper_reagba-x86_64.dylib | WKWebView，macOS 12+ |
| macos-arm64 | reaper_reagba-arm64.dylib | WKWebView，macOS 12+ |
| linux-x86_64 | reaper_reagba-x86_64.so | WebKitGTK 4.1，X11/XWayland |
| linux-aarch64 | reaper_reagba-aarch64.so | WebKitGTK 4.1，X11/XWayland |

Linux 的 `extension/reagba-webview-*` 是界面与原生 OpenGL 渲染辅助进程，用来隔离 REAPER 与 WebKitGTK 的 GDK 版本。GBA 核心、音频、存档仍在扩展中；辅助进程通过共享内存接收帧，像素不经过 JavaScript。原生 Wayland 停靠尚不支持。Ubuntu 24.04 的运行依赖为 `libwebkit2gtk-4.1-0 libepoxy0 libasound2t64 libcurl4t64`。

正式安装包不包含 `ReaGBA.exe`、验证程序、ROM、BIOS 或用户数据。macOS 包尚未签名或公证。

## 操作与布局

默认键盘：**W/S/A/D = 上/下/左/右，J/K = A/B，Q/E = L/R，回车 = Start，空格 = Select**。**按住 R 临时 4× 加速，松开恢复原倍率**。点击游戏画面获得输入焦点；编辑搜索或键位设置时会阻止游戏输入，离开 ReaGBA 后释放按键。设置中可修改或恢复默认键位，也支持 SDL GameController 兼容手柄。

右上角“停靠 / 取消停靠”切换 REAPER Docker 与浮动窗口，切换时保留正在运行的核心。界面只采用上下布局：游戏库在上，游戏画面在开始/暂停按钮上方。画面区域保持 GBA 的 3:2 比例；额外高度用于游戏库。拖动中间分隔条可改变占比，双击恢复自动分配。整数缩放可能产生少量黑边，可在设置中关闭。

支持暂停、继续、重置、停止、1×/2×/4× 倍率、音量、跳帧、9 个即时存档槽、电池存档、BMP 截图、收藏、搜索和最近游玩。只支持直接加载 `.gba`，ZIP 需先解压。

游戏库顶部可切换 **详细列表、封面网格、紧凑列表**，设置页也可选择，重开后保留。详细列表显示封面、文件大小与游玩时长；封面网格以大封面排列；紧凑列表缩小行高，仅保留小图标、标题、收藏和游玩按钮。

设置 → 游戏库 → **自动下载游戏封面** 默认关闭。开启后读取 `.gba` 文件头中的四位游戏编号，在本地匹配 [Libretro GBA 元数据](https://github.com/libretro/libretro-database/blob/master/metadat/no-intro/Nintendo%20-%20Game%20Boy%20Advance.dat)，再从 [Libretro GBA 封面库](https://github.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance) 下载对应地区的封面，不上传 ROM、文件名或本地路径。下载在独立线程完成，图片缓存到 `cache/covers`；关闭自动下载后，已缓存封面仍可离线显示。未匹配、下载失败或图片损坏时显示游戏编号首字母（编号为空时取标题首字符）。汉化/改版沿用原编号时通常显示原版封面，自制游戏或修改编号的 ROM 可能无封面。

封面编号索引缓存 30 天；未匹配结果缓存 7 天，网络错误退避 10 分钟，避免重复请求。到期后重新扫描或重开界面可重试。封面按完整比例显示，不裁剪；网格模式默认至少为一行封面留出空间，也可用分隔条调整。

设置 → 画面 → **Shader** 可选择关闭（默认）、**LCD3X** 或 **lcd-grid-v2**，选择立即生效并自动保存。Windows 的 D3D11 与 macOS/Linux 的 OpenGL 使用同一套效果公式；建议整数倍缩放，LCD3X 在 3× 以上更明显。Shader 启用时接管纹理采样，关闭后恢复原先的纹理过滤设置。效果内置在扩展/辅助进程中，不增加 `web` 文件；目前是两个固定预设，不支持导入任意 `.glslp` / `.slangp`。截图仍保存核心原始画面，不叠加显示 shader。

全部运行数据位于 REAPER 资源目录的 `Scripts/zaibuyidao Scripts/ReaGBA`：配置、存档、截图、缓存和 WebView 数据分别写入这里的对应子目录。不会读取或迁移 `Data/ReaGBA` 及旧 `Various/ReaGBA` 地址。设置中选择的 ROM 游戏文件夹和“打开 ROM”最后访问的文件夹都会保存。电池存档在暂停、切换游戏、正常关闭及定时检查时保存；即时存档按 ROM SHA-256 和核心版本核对。

## 构建与验证

需要 Git、Python 3、CMake 3.24+ 与 C++17 编译器。Windows 使用 Visual Studio 2022 C++ 工具链；macOS 使用 Xcode Command Line Tools。依赖自动获取到忽略提交的 `third_party` 或构建目录。

```sh
python scripts/build.py
```

该命令构建扩展、运行 CTest，并把可安装文件放入 `build/native/package`。仅运行核心检查可配置 `-DREAGBA_EXTENSION=OFF`。验证工具的可执行文件只用于开发，不进入安装包。

Linux 开发依赖：

```sh
sudo apt-get install build-essential cmake ninja-build git python3 pkg-config libx11-dev libgtk-3-dev libwebkit2gtk-4.1-dev libepoxy-dev libasound2-dev libudev-dev libcurl4-openssl-dev
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

设置页回归测试：`node tests/settings_verify.cjs`，检查不同宽高下无横向溢出、shader 保存/恢复、三份 UI 文件直接加载。Windows 的 `native_shaders` CTest 用 WARP 离屏渲染两个预设，与独立 CPU 数学参考逐像素抽样比对；运行 `reagba_shader_verify verification/shaders` 后还可执行 `node tests/shaders_gl_verify.cjs`，将同一 GLSL 程序转为 GLSL ES 3.0，在 WebGL2 中与 D3D 输出比对（不代替 macOS/Linux 实机 OpenGL 验收）。

游戏库回归测试：`node tests/library_verify.cjs`，覆盖三种视图、五种宽度、完整封面与字母回退、显示方式持久化、收藏/搜索/排序与键盘启动。`library_covers` CTest 使用替代下载器验证离线缓存、编号匹配、开关取消、损坏文件及重试限制，无需联网；`reagba_covers_verify --online BZME` 可选验证真实 HTTPS 封面下载。

Windows 本地已验证 REAPER 7.78 中注册操作、塞尔达 ROM 运行、音频设备、停靠/取消停靠及存读档。macOS 和 Linux 的桌面停靠、音频设备、实体手柄仍需对应系统实机验收；GitHub Actions 编译成功不能替代这些检查。详细验证范围见 [docs/VALIDATION.md](docs/VALIDATION.md)。

## 自动构建

私有源码仓库 `reagba` 独立构建。`ui/index.html`、`ui/style.css`、`ui/app.js` 本身就是可运行的发布文件，构建只把这三份文件原样复制到 `web`，不再内联合并。GitHub Actions 为五个平台编译、测试，生成安装 ZIP、原生扩展文件和 **ReaGBA-ReaPack-vX.Y.Z.zip**。ReaPack 发布包固定包含 7 个 `extension` 文件、3 个 `web` 文件和 `ReaGBA.ext`。

维护者自行从私有 Release 下载，将发布包中的 ReaGBA 目录整理进公开的 `ReaScripts/ReaGBA`，再提交并更新 ReaPack 索引。`.ext` 的所有下载地址只指向公开的 ReaScripts 仓库；运行时不需要私有源码仓库、构建工具或 GitHub 认证。工作流不会修改 ReaScripts，也不会自动跨仓库发布。

推送 main 时，工作流读取 CMake 版本号，首次出现该版本才创建 Release；已发布版本不会覆盖。提交新版本前修改 `CMakeLists.txt` 的版本号。普通分支及 PR 只产生构建产物。详见 [docs/RELEASING.md](docs/RELEASING.md)。

架构见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，依赖来源与许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
