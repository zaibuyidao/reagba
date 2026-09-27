# Native Service / Frame Stream 验证 — 2026-09-26

## v0.1.6 输入与设置验证 — 2026-09-27

- Windows x64 的 9 项 CTest、Linux x86_64 与 macOS ARM64 的各 6 项 CTest 通过，macOS Intel 交叉编译通过。覆盖手柄加速的保持与释放、键盘加速叠加、速度恢复、绑定持久化和默认游戏目录迁移。
- 浏览器验证设置页内嵌手柄绑定、独立 A/B 连发项及旧配置显示、普通与连发绑定互不替换、加速录入、紧凑双列布局、八种语言、保存失败回退及取消监听。共享输入测试覆盖主界面与游戏弹窗的全部 18 个存档快捷键、重复输入和编辑状态屏蔽。
- 发布清单包含本次设置页资源，移除独立手柄页面。`ReaGBA.ext` 与 GitHub Release 共用仅含 v0.1.6 改动的日志。Linux ARM64 由现有 CI 构建，本地未生成该平台产物。未执行实体手柄测试。

## v0.1.5 版本与界面验证 — 2026-09-27

- 8 项发布打包测试通过，校验包名、ReaPack 版本及发布日志生成。
- 手柄设置页通过点击监听、绑定替换、保存与取消、连发开关、八种语言及三种窗口宽度验证。
- 原生构建与测试记录见下方 v0.1.4，本次版本号调整后未重新编译原生安装包。

## v0.1.4 手柄绑定验证 — 2026-09-27

- Windows x64 构建及 9 项 CTest、Linux x86_64 构建及 6 项 CTest 通过。新增 `gamepad_bindings` 测试在所有原生 CI 平台启用，使用隔离的 SDL 虚拟手柄验证 16 个按键、双摇杆方向、扳机、失焦释放和断连释放。
- 映射测试覆盖每个输入到全部 GBA 操作、默认绑定、取消绑定、标准按下与释放、动作键可选 10 Hz 连发，以及旧单次触发配置迁移。多个输入绑定同一目标时合并按下状态。核心配置测试覆盖非法配置拒绝、缺省项补全和重启恢复。
- 独立手柄设置窗口验证逐项点击监听、原有绑定替换、原始按钮码、轴和方向帽录入、已按住输入过滤、取消、断连提示、标准别名替换，以及八种语言和三种窗口宽度。窗口不打开视频流、不发送游戏输入，关闭设置页保留主会话，主会话结束后自动关闭设置页。新增虚拟设备测试覆盖无标准映射设备的第 40 号按钮、轴、方向帽及原始绑定重启恢复。
- 设置页验证标准行为默认启用、仅动作键显示连发开关、开关与绑定保存、重载、恢复默认、保存失败回退及旧扩展忽略配置时的错误提示。十种窗口尺寸及八种语言各六种窗口尺寸检查通过，未发现横向溢出。Native Service 适配及发布打包测试通过，Release 正文与 ReaPack `@changelog` 仅包含本次更新。
- macOS ARM64 在 Apple Silicon / macOS 26.0.1 上构建成功，6 项 CTest 全部通过，包含手柄映射与触发测试。macOS Intel 交叉编译成功，构建机未安装 Rosetta，未执行 Intel 程序。两种架构均生成 v0.1.4 安装包。Linux ARM64 保留现有 CI 目标，本次未执行。未执行实体 PS4 蓝牙手柄测试。

## v0.1.3 窗口关闭验证 — 2026-09-27

- Windows x64 重新构建及 8 项 CTest 通过。Native Service 测试覆盖三种音频模式下主窗口消失且无页面清理通知的场景，确认核心与帧流销毁、旧请求不能重新创建核心；独立游戏页关闭及主窗口刷新保留主会话，重新打开主窗口创建空闲会话。
- 独立配置的 REAPER / WebView2 实测通过关闭与重新打开，确认原核心释放、显示偏好保留、重新加载游戏和存档正常；停靠/浮动及弹出游戏窗口回归通过。
- 前端接入测试覆盖清理期间的初始化和残留输入，设置页十种窗口尺寸、弹出页关闭/恢复及八种语言目录检查通过。音频输出的通用说明已从页面和语言目录删除。
- 本次未执行 macOS / Linux 实机关闭验证。

## v0.1.2 音频输出验证 — 2026-09-27

- Windows x64 构建及 8 项 CTest、Linux x86_64 构建及 5 项 CTest 通过。重采样覆盖 32768、44100、48000、96000 和 192000 Hz，以及块间连续性、立体声/单声道、欠载、模式切换、目标优先级与资源释放。
- 确定性时钟测试包含五组各 10 分钟的模拟运行，覆盖 ±1000/±3000 ppm、64–4096 帧输出块、GBA 帧突发和调度抖动。另有两小时 ±200 ppm 测试。所有场景在中点反转偏差方向，启动后无欠载、溢出或强制重同步，缓冲保持有界。单独验证生产中断后的预缓冲及过期积压丢弃，SDL S16 与 REAPER 浮点路径均覆盖。
- Windows 和 Linux 隔离 REAPER / Dummy Audio 实测通过。原生探针验证指定第二通道的单声道输出、其他通道静音、通道缺失静音及恢复、音频设备关闭和重启。REAPER 轨道模式验证 FX、音量、Pan、Mute/Solo、Master Send、轨道发送、同名轨道优先级、选中轨道跟随、无目标静音、播放/停止、工程切换、暂停/加速和销毁。测试音源为本仓库生成的原创 GBA 方波程序，不含商业 ROM。
- 设置界面验证三种模式、两种目标规则及硬件通道的保存与恢复，设备清单变化后保留选择，以及十种窗口尺寸无横向溢出。八种语言目录与 Native Service 回归通过。
- macOS 使用同一 REAPER SDK 和 pthread 预听生命周期实现，保留现有 Intel/ARM64 CI 目标。本次未执行 macOS 实机音频测试。

复现真实混音测试：先构建测试目标，然后运行 `python tests/audio_host_verify.py --output build/audio-host-check`。非 Windows 环境通过 `--reaper`、`--core`、`--probe` 指定对应二进制。测试探针不会进入发行包。

## v0.1.1 窗口与布局验证 — 2026-09-27

- Windows x64 构建及 7 项 CTest 通过。隔离 REAPER / WebView2 验证独立游戏页及原生标题、暂停画面重放、持续按键、释放按键和收回恢复，原有停靠、存档、Shader 与关闭重开流程通过。
- 浏览器回归覆盖缩短窗口时保留画面尺寸、空间不足时滚动、折叠后信息置顶、控制置底及画布居中，以及分隔条调整。详情卡片验证覆盖封面居中和按钮边界。双窗口测试覆盖 3px 页面间距、状态栏间距一致性、游戏库展开或折叠时弹出及收回的画布位置一致性、设置同步、输入归属、关闭恢复、重复弹出、打开失败和主窗口重载或关闭。
- Windows、macOS、Linux 共用网页与 ReaWebAPI 窗口接口。本版本的 macOS / Linux 原生窗口行为尚未实机复测，下表保留此前验证记录。

配套运行时为 ReaWebAPI v0.3.6.4。ReaGBA 保持独立仓库和自身版本号，核心、UI 与适配测试均在本仓库维护。

| 平台 | 构建与自动测试 | 真实 REAPER / Apotris |
| --- | --- | --- |
| Windows x64 / MSVC | Release 构建成功，7/7 CTest 通过 | Native Service、二进制帧与 WebGL 界面通过，模拟线程约 59.87 Hz |
| macOS ARM64 / AppleClang | Apple M4、macOS 26.0.1 构建成功，4/4 CTest 通过 | REAPER 7.74 全部界面流程通过，模拟线程约 59.22 Hz |
| macOS Intel | x86_64 交叉编译成功 | 测试 Mac 没有 Rosetta，未执行 Intel 实测 |
| Linux x86_64 / GCC | 核心构建成功，4/4 CTest 通过 | REAPER 7.78 全部界面流程通过，模拟线程约 59.96 Hz |
| Linux ARM64 | 保留现有 CI 目标 | 未在本地执行 |

## 覆盖范围

- 三平台原生扩展测试均加载用户提供的 Apotris，验证服务注册、控制、即时输入、RGBA 像素、存读档，以及超过 1 MiB 的封面结果分块。ReaWebAPI 先卸载与 ReaGBA 先卸载两种顺序均通过，流关闭后没有继续发布或调用已卸载 API。
- Windows/macOS/Linux 实机界面验证覆盖 Lua 启动器立即返回、游戏库、按下/释放、临时加速、暂停/继续、九槽接口中的存读档、截图、三个 Shader 选项、停靠/浮动、关闭重开、暂停画面重放、偏好和存档持久化、ROM 关闭/重载/重置。
- 页面忙等 800 ms 时，模拟线程继续运行，恢复后读取新帧。核心使用自己的约 59.73 Hz 时钟。短时测量不代表所有 ROM、设备和显示刷新率。Linux WSLg 测试使用软件 GL、`WEBKIT_DISABLE_DMABUF_RENDERER=1` 和 SDL dummy 音频，未验证该环境的游戏音频听感。
- 旧七个 ReaScript 核心 API 的注册及调用回归通过，不要求安装 ReaWebAPI。Native Service 与流接口为增量接入。
- JavaScript 适配测试验证服务错误、状态、输入、定时器清理、大结果分块、偏好和二进制帧边界。WebGL 与 Canvas fallback 的 36 个尺寸/Shader 组合和独立 D3D 参考比对通过，最大通道差为 1/255。
- ReaGBA 发布测试验证自己的核心包和 ReaPack 描述。ROM、存档、截图、测试输出及 ReaWebAPI 二进制不进入本仓库发布包。

实体手柄、商业游戏长时间运行和用户音频设备的听感未在本次自动验证中覆盖。旧版本记录中的 ReaGBA PCM 使用 SDL 输出，v0.1.2 的音频验证见上方记录。内建音频分析属于 ReaWebAPI，其三平台验证记录在对应仓库。

## 复现

```sh
python scripts/build.py
ctest --test-dir build/native -C Release --output-on-failure
node tests/BridgeTests.cjs
node tests/frame_decode.cjs
node tests/reaweb_video.cjs
node tests/layout_verify.cjs
node tests/popout_verify.cjs
node tests/gamepad_settings.cjs
```

前端测试默认读取本仓库 `web/`。浏览器测试需要 Playwright，可用 `REAGBA_NODE_MODULES` 指定模块目录，`REAGBA_BROWSER_CHANNEL=msedge` 使用本机 Edge。

```sh
python tests/native_service_api.py --core build/native/bin/Release/reaper_reagba-x64.dll --rom /path/to/test.gba --output build/native-service-check
python tests/reawebapi_smoke.py --reaper C:/REAPER/reaper.exe --reawebapi ../ReaWebAPI/build/Release/reaper_reawebapi-x64.dll --rom /path/to/test.gba --audio-config C:/REAPER/reaper.ini --output build/native-ui-check
```

输出目录必须尚不存在。实机测试只创建隔离 REAPER 配置，不替换已安装扩展、日常配置或项目。`--rom` 省略时，界面测试生成一个简单的原创 ARM 测试程序。帧率与兼容性结论使用实际 Apotris ROM，不分发该 ROM。
