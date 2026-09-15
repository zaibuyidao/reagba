# 验证范围

## Windows 本地

在 REAPER 7.78 的隔离资源目录中，原生 DLL 注册 `_REAGBA_SHOW` 成功。通过 WebView 加载本地塞尔达 GBA ROM，正常运行约 59.7 FPS，打开原生音频设备；暂停、保存槽位 9、重置、读取槽位 9、继续、停靠、取消停靠、修改设置和截图的 11 次操作均返回成功，取消停靠后原生游戏区恢复。

CTest 覆盖核心参数与缓冲边界、默认键位及短按/组合键/失焦/加速、WebView2 和 WebKit 请求 ID/异步回复/错误/超时，以及安装包白名单和不可覆盖的发布行为。实际 ROM 检查另验证 GBA KEYINPUT 寄存器、画面变化、PCM、存档重放和电池存档。

布局自动检查覆盖 298×1299 至 1920×400 的 8 种尺寸、拖动分隔条、占比保存/恢复、边界限制和双击复位。界面只有上下排列。

路径测试确认配置、存档、截图、缓存和 WebView 数据都位于 `Scripts/zaibuyidao Scripts/ReaGBA`，且旧地址不会被使用。安装 ZIP 的 `web` 只含三份可直接运行且与源码一致的 UI 文件；`.ext` 使用实际 reapack-index 检查五个平台的 22 个 source、公开 ReaScripts 提交链接及不注册脚本的文件映射。打包测试覆盖固定的 7 个扩展文件、3 个 UI 文件、五平台汇总、公共 UI 一致性、私有数据排除和缺失文件。

## 跨平台边界

WSL Ubuntu 24.04 中已成功编译 Linux x86_64 扩展和 GTK3/WebKitGTK 辅助进程，核心边界、发布包以及共享帧传输/渲染进程持锁退出后的恢复测试全部通过。macOS 提供 SWELL + WKWebView + NSOpenGLView 实现。五平台由 GitHub Actions 编译验证。

本机不是 macOS，也未在原生 Linux REAPER 桌面内完成 Docker、键盘、音频和实体手柄验收。GitHub 工作流需由仓库所有者提交后实际运行；本地结果不代表远端 CI 已通过。ROM 测试不代表所有游戏兼容或通关测试。

## 复现

- `python scripts/build.py`：编译和无 ROM 测试。
- `reagba_verify <ROM.gba> <output> 7200`：核心、音频与存档。
- Windows `reagba_input_verify <ROM.gba> <output.json>`：默认按键与真实核心寄存器。
- `python scripts/extension_smoke.py --reaper <reaper.exe> --rom <ROM.gba>`：隔离 REAPER 扩展测试。
- `node tests/layout_verify.cjs`：安装 Playwright 后验证响应式界面。
- `ruby tests/ReaPackIndex.rb <generated ReaGBA.ext> <temporary index.xml>`：安装 reapack-index 1.2.3 后验证真实索引格式，不写公开仓库的 index.xml。
- 手动：新建空工程打开 ReaGBA，点击原生画面，逐个按 W/S/A/D、J/K、Q/E、回车、空格、R，检查失焦释放、搜索不触发游戏键、停靠及拖动。

所有测试输出保留在被 gitignore 排除的 verification/build 中，不进入源码仓库或安装包。

`tests/ExtensionSmoke.lua` 仅是隔离 REAPER 测试驱动，不是启动器，也不会被安装。测试配置关闭首次启动的 VST 路径迁移、版本更新和设备提示，避免这些宿主弹窗阻塞扩展计时器；不修改日常 REAPER 配置。
