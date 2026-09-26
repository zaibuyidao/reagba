# ReaGBA

ReaGBA 是 REAPER 内的 **GBA 核心扩展**。扩展提供 mGBA 模拟、音频、游戏输入、ROM 游戏库、封面、存档和截图接口，不注册操作列表入口，也不创建或管理 WebView。

界面位于本仓库的 `ui/`，由 **ReaWebAPI v0.3.6.4+** 承载：

```text
reagba/ui/
  Open.lua
  index.html
  style.css
  i18n.js
  app.js
```

运行关系为 `Lua 启动脚本 → ReaWebAPI WebView → reagba Native Service / Frame Stream → ReaGBA Core`。窗口、停靠、位置恢复、文件选择和原生通信由 ReaWebAPI 提供。前端保留原有布局、样式、八种语言、游戏库三种视图、搜索/收藏、设置、分隔条和全部模拟控制。

## 安装与打开

1. 完全退出 REAPER，将新构建的 `reaper_reagba-x64.dll` 放入 `C:\REAPER\UserPlugins`，并安装 ReaWebAPI v0.3.6.4 或更高版本的扩展。
2. 保持 `reagba/ui` 的五个文件在同一目录。
3. 重启 REAPER，在操作列表加载并运行该目录的 `Open.lua`。脚本描述为 **ReaGBA (ReaWebAPI)**。
4. 点击“打开 ROM”，选择 `.gba` 文件；也可以在设置中选择 ROM 文件夹。

旧原生入口 `zaibuyidao: ReaGBA` / `_REAGBA_SHOW` 已移除。已有快捷键、工具栏请绑定 Lua 脚本。重复执行启动脚本会激活同一窗口；启动脚本打开窗口后立即结束。关闭窗口会释放该页面的订阅，核心会话保留到显式销毁或扩展卸载，输入超过 750 ms 未刷新会自动释放。

默认按键：W/S/A/D 为方向，J/K 为 A/B，Q/O 为 R/L，回车为 Start，空格为 Select；按住 L 临时 4× 加速。支持自定义按键和 SDL GameController。编辑搜索、设置或失焦时释放游戏输入。默认音量 30%。

保留暂停/继续、重置、停止、1×/2×/4×、音量、跳帧、九个即时存档槽、电池存档、BMP 截图、最近游玩和可选封面下载。支持整数缩放、最近邻/线性过滤、LCD3X 和 lcd-grid-v2；显示和 Shader 在 WebView 内执行，PCM 仍由核心的 SDL 音频设备输出。

核心数据继续使用 `<REAPER 资源目录>/Scripts/zaibuyidao Scripts/ReaGBA`，原有存档和 ROM 路径保持可用。WebView 的显示偏好通过 ReaWebAPI 写入该目录的 `config/ui.json`，首次使用会读取旧 `preferences.json` 中的显示偏好。窗口与浏览器数据由 ReaWebAPI 管理；旧宿主的 `window.json` 不再使用。

## 构建

需要 Python 3、CMake 3.24+、C++17 工具链；Windows 使用 Visual Studio 2022 x64：

```sh
python scripts/build.py
```

生成的 DLL 位于 `build/native/bin/Release/reaper_reagba-x64.dll`，安装组件只向 `UserPlugins` 安装核心二进制。核心构建、平台安装包和 ReaPack 核心包均不再携带 Web 文件、WebView2 Loader 或 Linux WebView 辅助进程。UI 在本仓库 `ui/` 维护，不需要重新编译核心即可修改界面。

Linux 构建依赖为 C/C++ 工具链、CMake、pkg-config、SDL 音频/手柄所需开发库和 libcurl，不再要求 WebKitGTK。macOS 的 Objective-C/SWELL 支持仅用于 SDL 和 REAPER SDK 兼容，不包含窗口宿主。

验证方法见 [docs/VALIDATION.md](docs/VALIDATION.md)，核心接口和生命周期见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
