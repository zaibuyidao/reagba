# 验证范围

## 多语言改动（2026-09-17）

Windows x64 扩展已本地构建。原生配置测试验证首次英文、八种语言及未来的 `pt-BR` 标识可保存、无效输入被拒绝、重建 EmulatorManager 后保留语言。语言资源测试校验八种语言各 127 条文案、占位符、英文回退、数字格式和 DOM 文案键。

`i18n_verify.cjs` 在关闭 localStorage 的模拟原生桥中验证八语言即时切换、六种窗口尺寸（240、298、320、440、760、1280 像素宽）、重开恢复、失败回退、旧扩展提示、未保存输入与所选槽位保留、文件对话框标题及常见错误翻译。设置页 10 尺寸、游戏库三视图五尺寸、原有 8 尺寸布局回归也已通过。窄长窗口仍能完整显示至少五条详细列表记录。

打包测试已更新为四个 UI 文件并检查 `i18n.js`；实际 reapack-index 校验五平台 27 个 source 通过，Windows 安装 ZIP 的五个安装文件及校验和已复核。未对本次改动执行 REAPER 实机或 macOS/Linux 运行验证。以下为原有功能的验证记录，其中三文件包对应多语言改动之前的版本。

## Windows 本地

在 REAPER 7.78 的隔离资源目录中，原生 DLL 注册 `_REAGBA_SHOW` 成功。通过 WebView 加载本地塞尔达 GBA ROM，正常运行约 59.7 FPS，打开原生音频设备；暂停、保存槽位 9、重置、读取槽位 9、继续、停靠、取消停靠、修改设置和截图的 11 次操作均返回成功，取消停靠后原生游戏区恢复。

CTest 覆盖核心参数与缓冲边界、默认键位及短按/组合键/失焦/加速、WebView2 和 WebKit 请求 ID/异步回复/错误/超时，以及安装包白名单和不可覆盖的发布行为。实际 ROM 检查另验证 GBA KEYINPUT 寄存器、画面变化、PCM、存档重放和电池存档。

布局自动检查覆盖 298×1299 至 1920×400 的 8 种尺寸、拖动分隔条、占比保存/恢复、边界限制和双击复位。界面只有上下排列。

本次 LCD Shader 检查：Windows D3D11/WARP 的 24 组离屏渲染（关闭、LCD3X、lcd-grid-v2、再关闭；0.6×、1×、2×、3×、4×及非整数缩放）与独立双精度 CPU 参考比对，最大色阶误差 1/255。导出的公用 GLSL 转为 GLSL ES 3.0 后，通过 WebGL2 编译、绘制 18 组相同画面，与 D3D 输出最大差异 1/255。覆盖边缘夹取与不透明 alpha；最近邻采样恰落在两个源像素分界处时允许硬件舍入差异。以上检查不等同于 macOS/Linux 原生驱动实测。

2026-09-16 已使用固定版本 mGBA / SDL / JSON / REAPER SDK / WebView2 完成 Windows Release DLL 的完整编译及安装目录生成；6/6 CTest 通过（原生核心不变量、shader、键盘、运行路径、打包、WebView 通信）。配置测试通过真实 `EmulatorManager` 写入/重读 preferences.json，确认 shader 默认关闭、三个预设可用、非法值被拒绝、重启后保留选择。本次未重新进行真实 ROM 的 REAPER 宿主 smoke 测试，也未生成 macOS/Linux 新二进制。

`settings_verify.cjs` 用三份实际 UI 文件及模拟原生桥，在 240×500 至 1920×2000 的 10 种尺寸中确认设置区域 `scrollWidth <= clientWidth`、BIOS 按钮可达、冗余键位说明已移除、shader 选项保存/重开恢复，以及 JavaScript/CSP 无错误。浏览器测试显式保留滚动条；修复前该测试定位到音量 range 的默认左右 margin 导致 4 像素溢出。

2026-09-16 游戏库封面与显示方式：Windows Release DLL 构建通过，8 项 CTest 全部通过（含新增 `library_covers`）；实际 WinHTTP 请求根据 BZME 编号下载到《塞尔达传说：缩小帽》PNG。封面测试覆盖改名 ROM 的内部编号、默认不联网、下载开关、串行后台队列、缓存重开/离线读取、缺失封面、损坏图片/索引/状态缓存恢复、失败重试限制和取消。WSL Ubuntu 24.04 使用系统 libcurl 编译同一封面模块并通过其原生测试；未重新构建完整 Linux 扩展或验证 macOS。

`library_verify.cjs` 用真实三份 UI 和模拟原生桥，在 240、298、440、760、1280 五种宽度检查详细/网格/紧凑三种模式无横向溢出，网格行高容纳完整封面，紧凑行高不超过 48px；检查缓存图片、异步下载、失败字母回退、设置持久化、搜索/收藏/排序、回车启动。三种模式截图已目视检查；原有 8 尺寸布局和 10 尺寸设置回归均通过。本次未重新运行 REAPER 宿主 smoke 测试。

路径测试确认配置、存档、截图、缓存和 WebView 数据都位于 `Scripts/zaibuyidao Scripts/ReaGBA`，且旧地址不会被使用。安装 ZIP 的 `web` 只含三份可直接运行且与源码一致的 UI 文件；`.ext` 使用实际 reapack-index 检查五个平台的 22 个 source、公开 ReaScripts 提交链接及不注册脚本的文件映射。打包测试覆盖固定的 7 个扩展文件、3 个 UI 文件、五平台汇总、公共 UI 一致性、私有数据排除和缺失文件。

2026-09-17 窗口及偏好恢复：Windows Release 编译和 8 项 CTest 通过。新增原生检查验证窗口配置兼容旧停靠字段、非法尺寸回退、默认 Q/O 肩键、L 加速、30% 音量，以及自定义键位/音量和展开状态的配置重读。`settings_verify.cjs` 验证全部 11 个键位修改后的启动提示、打开 ROM 后的提示、重开及恢复默认；`layout_verify.cjs` 验证收起/展开重开恢复；`library_verify.cjs` 验证三种显示方式分别重开恢复。三组浏览器检查通过。

`window_persistence.py` 在隔离 REAPER 7.78 配置中依次启动五次，验证浮动 880×640、位置 (180,160)、停靠退出、同一 Docker 重开、取消停靠及浮动重开。此测试定位并覆盖了 REAPER 先销毁 Docker、再卸载扩展导致停靠状态被覆盖的问题。未在 macOS/Linux 原生宿主中复测这些窗口行为。

## 跨平台边界

WSL Ubuntu 24.04 中已成功编译 Linux x86_64 扩展和 GTK3/WebKitGTK 辅助进程，核心边界、发布包以及共享帧传输/渲染进程持锁退出后的恢复测试全部通过。macOS 提供 SWELL + WKWebView + NSOpenGLView 实现。五平台由 GitHub Actions 编译验证。

本机不是 macOS，也未在原生 Linux REAPER 桌面内完成 Docker、键盘、音频和实体手柄验收。GitHub 工作流需由仓库所有者提交后实际运行；本地结果不代表远端 CI 已通过。ROM 测试不代表所有游戏兼容或通关测试。

## 复现

- `python scripts/build.py`：编译和无 ROM 测试。
- `reagba_verify <ROM.gba> <output> 7200`：核心、音频与存档。
- Windows `reagba_input_verify <ROM.gba> <output.json>`：默认按键与真实核心寄存器。
- `python scripts/extension_smoke.py --reaper <reaper.exe> --rom <ROM.gba>`：隔离 REAPER 扩展测试。
- `python tests/window_persistence.py --reaper <reaper.exe>`：无需 ROM 的隔离 REAPER 窗口/停靠重启测试。
- `node tests/layout_verify.cjs`：安装 Playwright 后验证响应式界面。
- `node tests/settings_verify.cjs`：设置页滚动与 shader 选择保存测试。
- `node tests/library_verify.cjs`：三种游戏库视图、封面回退和交互回归。
- `reagba_covers_verify`：无需网络的封面模块测试；追加 `--online BZME` 检查真实来源，写入 `verification/covers-online`。
- Windows `reagba_shader_verify verification/shaders`：D3D 编译/像素参考测试，导出 GPU 结果。
- `node tests/shaders_gl_verify.cjs`：以上导出结果的 WebGL2/GLSL ES 跨后端比对。
- `ruby tests/ReaPackIndex.rb <generated ReaGBA.ext> <temporary index.xml>`：安装 reapack-index 1.2.3 后验证真实索引格式，不写公开仓库的 index.xml。
- 手动：新建空工程打开 ReaGBA，点击原生画面，逐个按 W/S/A/D、J/K、Q/O、回车、空格、L，检查失焦释放、搜索不触发游戏键、停靠及拖动。

所有测试输出保留在被 gitignore 排除的 verification/build 中，不进入源码仓库或安装包。

`tests/ExtensionSmoke.lua` 仅是隔离 REAPER 测试驱动，不是启动器，也不会被安装。测试配置关闭首次启动的 VST 路径迁移、版本更新和设备提示，避免这些宿主弹窗阻塞扩展计时器；不修改日常 REAPER 配置。
