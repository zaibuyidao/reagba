# ReaGBA

ReaGBA 是 REAPER 内的 **GBA 核心扩展**。扩展提供 mGBA 模拟、音频、游戏输入、ROM 游戏库、封面、存档和截图接口，不注册操作列表入口，也不创建或管理 WebView。

界面位于本仓库的 `web/`，由 **ReaWebAPI v0.3.6.4+** 承载：

```text
reagba/web/
  zaibuyidao_ReaGBA.lua
  index.html
  style.css
  i18n.js
  app.js
  bridge.js
  video.js
  popout.js
  game.html
  game.js
  gamepad.js
```

运行关系为 `Lua 启动脚本 → ReaWebAPI WebView → reagba Native Service / Frame Stream → ReaGBA Core`。窗口、停靠、位置恢复、文件选择和原生通信由 ReaWebAPI 提供。前端保留原有布局、样式、八种语言、游戏库三种视图、搜索/收藏、设置、分隔条和全部模拟控制。

## 安装与打开

1. 完全退出 REAPER，将对应平台的 `reaper_reagba-<arch>.*` 放入 REAPER 资源目录的 `UserPlugins/`，并安装 ReaWebAPI v0.3.6.4 或更高版本的扩展。
2. 将 ReaPack 压缩包中的 `ReaGBA/` 放入 `<REAPER 资源目录>/Scripts/zaibuyidao Scripts/Modules/`，保持 `ReaGBA/web/` 的所有文件在同一目录。
3. 重启 REAPER，在操作列表加载并运行 `ReaGBA/web/zaibuyidao_ReaGBA.lua`。脚本描述为 **ReaGBA (ReaWebAPI)**。
4. 点击“打开 ROM”，选择 `.gba` 文件；也可以在设置中选择 ROM 文件夹。

旧原生入口 `zaibuyidao: ReaGBA` / `_REAGBA_SHOW` 已移除。已有快捷键、工具栏请绑定 Lua 脚本。重复执行启动脚本会激活同一窗口；启动脚本打开窗口后立即结束。关闭主窗口（窗口 X 或停靠分页 X）会停止模拟、保存电池存档并释放全部音频输出；关闭独立游戏窗口则收回主界面。输入超过 750 ms 未刷新会自动释放。

点击“弹出游戏画面”可在独立浮动窗口中显示游戏。弹窗标题为 `ReaGBA - 游戏名称`，页面只保留四周边距为 3px 的画布。关闭弹窗或点击主界面的“恢复到主界面”可收回画面。无论游戏库展开或折叠，弹出后主界面均保留原位置和尺寸的空画布。游戏库折叠时，游戏信息置顶、控制区置底，画布在中间居中。弹出时画布留空，收回后在相同位置恢复游戏图像。缩短主窗口高度时保留游戏画面尺寸，通过滚动访问超出区域。展开时仍可通过分隔条主动调整画面大小。

默认按键：W/S/A/D 为方向，J/K 为 A/B，O/Q 为 L/R，回车为 Start，空格为 Select；按住 L 临时 4× 加速。支持自定义按键和 SDL GameController。编辑搜索、设置或失焦时释放游戏输入。默认音量 30%。

v0.1.7 的手柄绑定与键盘映射使用一致的紧凑双列布局，顺序为 A/B、A 连发/B 连发、Select/Start、Up/Down、Left/Right、L/R、加速。方向及手柄输入名称使用文字，不显示箭头。点击 GBA 操作旁的绑定项，再按下手柄按键即可录入，支持 SDL 报告的任意按钮编号、轴方向和方向帽，不要求设备具有标准 GameController 映射。每个 GBA 操作显示一个绑定按钮，点击后显示“请按键…”，识别输入后替换该操作的原有绑定并显示按键码。Esc 取消录入。录入前已按住的输入须松开后再按。

键盘的 A 连发、B 连发默认未绑定，点击后可录入按键。连发键与普通 A/B 独立保存，重启后恢复，恢复默认键盘映射时清空连发绑定。

普通绑定采用标准按键行为，按住时保持，松开后释放。普通 A/B 的下一行提供独立的“A 连发”“B 连发”绑定，可分别录入任意手柄按键，与普通 A/B 绑定互不替换。连发每秒 10 次，按下和释放各 50 ms。已有 A/B 连发配置显示在对应的连发项目中。L/R、方向、Start、Select 保持标准行为。“加速（按住）”可绑定任意手柄输入，按住时临时切换到 4×，松开后恢复所选速度。旧 L/R 连发配置转为标准行为。旧单次触发配置自动转为标准行为，按键绑定保留。

默认 A/B 对应 GBA A/B，肩键对应 L/R，Back/Start 对应 Select/Start，方向键及左摇杆对应 GBA 方向。其余输入默认不绑定，默认采用标准按键行为。配置自动保存到 `config/preferences.json` 的 `gamepad_bindings`，下次启动自动恢复，可单独恢复默认手柄绑定。Windows、macOS、Linux 使用同一映射和触发逻辑。当前轮询首个可用手柄，配置为全局配置。原始编号由设备和驱动决定，更换手柄或平台后可能需要重新录入。

游戏界面及弹出游戏窗口支持 `Ctrl+1` 至 `Ctrl+9` 保存到对应槽位，`Shift+1` 至 `Shift+9` 读取对应槽位。长按不重复触发，设置页、文字输入及未加载游戏时不触发。

保留暂停/继续、重置、停止、1×/2×/4×、音量、跳帧、九个即时存档槽、电池存档、BMP 截图、最近游玩和可选封面下载。支持整数缩放、最近邻/线性过滤、LCD3X 和 lcd-grid-v2。显示和 Shader 在 WebView 内执行。

设置中的“音频输出”提供三种模式，选择保存在核心配置中：

- **系统默认设备（System default device）**：默认兼容模式，通过 SDL 直接使用系统默认音频设备。
- **REAPER 硬件输出（REAPER hardware output）**：从 REAPER 当前设备中选择立体声通道对或单声道输出，默认通道 1/2。不经过 Master FX 与 Master 音量。
- **REAPER 轨道（REAPER track）**：音频进入当前工程的目标轨道，经过 FX、音量、Pan、Routing 和 Mute/Solo。默认使用第一条名为 `ReaGBA Preview` 的轨道，没有同名轨道时使用第一条选中轨道。也可选择始终跟随第一条选中轨道。没有目标时保持静音，不创建轨道。

两种 REAPER 模式均由 REAPER 音频引擎拉取 PCM，不单独打开系统音频设备，也不添加 FX。切换模式、轨道或工程时释放旧输出并重新绑定。暂停、停止和加速时保持静音。REAPER 的音频设备需处于运行状态，这些模式用于实时预听，不会创建可离线渲染的媒体。

硬件设备、采样率和设备缓冲由 REAPER 管理。输出通道按当前设备的通道序号保存，列表随设备配置更新。所选通道不可用时保留设置并静音，恢复后自动重连。三种模式均自动校正长期时钟漂移，欠载后重新预缓冲，中断恢复时丢弃过期音频。无需手动设置重采样率。轨道 FX 和硬件缓冲仍会增加预听延迟。

Windows、macOS 和 Linux 的核心数据统一位于 `<REAPER 资源目录>/Scripts/zaibuyidao Scripts/Modules/ReaGBA`，包含 `config/`、`cache/`、`saves/`、`screenshots/`、`states/` 和默认游戏目录 `roms/`。首次使用时，“游戏文件夹”显示此目录下 `roms/` 的完整路径，已有自定义路径保持不变。升级时将旧目录中的数据移至此处，将 `ROM/` 改名为 `roms/`，如已选择的游戏目录发生变化，请在设置中重新选择。WebView 的显示偏好通过 ReaWebAPI 写入 `config/ui.json`，首次使用会读取旧 `preferences.json` 中的显示偏好。窗口与浏览器数据由 ReaWebAPI 管理，旧宿主的 `window.json` 不再使用。

## 构建

需要 Python 3、CMake 3.24+、C++17 工具链；Windows 使用 Visual Studio 2022 x64：

```sh
python scripts/build.py
```

生成的 DLL 位于 `build/native/bin/Release/reaper_reagba-x64.dll`，安装组件和平台安装包只向 `UserPlugins` 安装核心二进制。`ReaGBA-ReaPack-v0.1.7.zip` 的 `ReaGBA/` 下包含 `extension/`、`web/` 和 `ReaGBA.ext`。界面在本仓库 `web/` 维护，不需要重新编译核心即可修改界面。

Linux 构建依赖为 C/C++ 工具链、CMake、pkg-config、SDL 音频/手柄所需开发库和 libcurl，不再要求 WebKitGTK。macOS 的 Objective-C/SWELL 支持仅用于 SDL 和 REAPER SDK 兼容，不包含窗口宿主。

验证方法见 [docs/VALIDATION.md](docs/VALIDATION.md)，核心接口和生命周期见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
