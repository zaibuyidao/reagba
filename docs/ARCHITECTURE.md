# 架构与接口

## 职责

```text
web/zaibuyidao_ReaGBA.lua → ReaWebAPI WebView
  ├─ reaper.host.service("reagba") → Native control / atomic input → mGBA
  └─ reaper.stream.open("reagba.video") ← independent binary transport ← emulator thread
```

ReaGBA 不链接任何浏览器 SDK，不创建窗口，不注册 command_id、gaccel、hookcommand 或 hwnd_info。扩展的 timer 负责服务延迟注册、游戏手柄轮询、输入超时和控制回复缓存过期。`src/bridge/CoreCommands` 是独立于 UI 的核心命令分发器。

ReaWebAPI 直接加载普通 HTML 目录并注入 `window.reaper`，不复制 runtime/reaper.js、不修改 ReaWebAPI 的基础运行架构。原生各平台 WebView 宿主由 ReaWebAPI 维护。旧 ReaGBA Windows/macOS/Linux 宿主和 Linux UI 辅助进程已删除。

## 核心 ReaScript API

所有入口从 REAPER 主线程调用。错误返回 false / 0 / 空串，并由 `ReaGBA_GetLastError()` 提供说明。

| API | 返回与用途 |
| --- | --- |
| `ReaGBA_Create(dataDirectory)` | 正整数会话 ID；空目录使用原有 ReaGBA 数据目录。一次只允许一个核心，避免同时写同一存档 |
| `ReaGBA_Destroy(id)` | 停止工作线程、完成电池存档、释放音频和手柄 |
| `ReaGBA_Request(id, json)` | 接收不超过 64 KiB 的 `{action, id, ...}` 异步命令 |
| `ReaGBA_Poll(id)` | 非阻塞读取 `{type:"reply",id,action,ok,result/error}`；空串表示暂无回复 |
| `ReaGBA_ReadFrame(id, force)` | 读取最新 240×160 图像的 base64 JSON（RGBA 或无损 RLE）；无新帧时为空，force 可重取最后画面 |
| `ReaGBA_SetInput(id, mask, fastForward, active)` | mask 位序为 A/B/Select/Start/Right/Left/Up/Down/R/L；750 ms 未刷新即停止输入 |
| `ReaGBA_GetLastError()` | 上一次同步 API 调用的错误文本 |

控制命令包括加载 ROM、开始/暂停/重置/停止、九槽存读档、电池存档、截图、音量、倍率、跳帧、状态、ROM 扫描、封面、收藏和核心配置。`game_viewport`、`toggle_dock`、`open_rom` 等 UI 命令不属于核心接口。

CoreCommands 保持原有核心命令结构；回复 ID 由适配层补回。已接收而未取回的请求最多 128 个，结果最多 8 MiB。超限明确返回错误，不静默丢失回复。会话 ID 单调增长，过期 Lua 回调不能操作重开的核心。

## Native Service 与界面

`web/zaibuyidao_ReaGBA.lua` 只打开或激活窗口后返回。界面和接入代码属于 ReaGBA 仓库，ReaWebAPI 提供通用窗口、服务与流能力。

`reagba` 服务支持 `loadRom`、`closeRom`、`pause`、`resume`、`reset`、`saveState`、`loadState`、`getState`、`setSpeed`、`settings`，也接受原有 CoreCommands 的 action 名称。普通命令异步执行。大型封面或游戏库结果通过有界分块读取，保留原有 8 MiB 结果上限。每片最多 128 KiB，缓存最多四项和 16 MiB，绑定请求窗口，60 秒后过期。帧流不使用此控制回复通道。

`service.send('input', {mask, fast, active})` 在主线程直接更新原子输入状态，不排入 ROM/存档命令队列。失焦或清理页面时释放输入，活跃页面每 200 ms 刷新输入有效期。750 ms 无刷新时自动释放。

`reagba.video` 为 240×160、RGBA8、960-byte stride 的 Frame Stream。模拟线程发布完整二进制帧，使用三槽有界缓冲。页面卡顿时保留最新帧，WebGL 直接接收 Uint8Array。消费者关闭只解除自己的连接，生产者关闭时通知所有页面。重开窗口可以读取暂停前的最后一帧。

界面保留原有布局、语言和控制。显示偏好使用 `config/ui.json`，核心维护模拟、ROM、封面和存档配置。对话框、停靠和聚焦复用 ReaWebAPI。

ReaWebAPI 或 ReaGBA 卸载时，服务关闭回调先停止并等待模拟线程，再关闭流和释放会话。ReaGBA 主动卸载时随后注销服务。旧 ReaScript 调用不要求安装 ReaWebAPI。

## 画面、音频与持久化

mGBA 在工作线程独占运行，像素发布至原生流并保留旧核心 API 的三缓冲，PCM 通过原有 SPSC 缓冲交给 SDL 音频回调。读取画面不阻塞模拟线程，截图仍是未经 Shader 处理的核心 BMP。

WebView 使用 WebGL2 显示、整数缩放及原有 LCD 效果公式。参考公式与原生 D3D 数学验证只保留于 `tests/reference/video`，不链接进扩展。没有 WebGL2 时使用 Canvas 2D 与相同的 CPU Shader 公式回退显示，性能取决于窗口大小和设备。

模拟与音频保持约 59.73 Hz 的核心时钟。帧传输不依赖 Lua defer、普通服务 RPC 或 Runtime::tick()。vsync 控制是否通过 requestAnimationFrame 提交；浏览器仍负责最终合成与节流。

核心数据目录为 `<REAPER 资源目录>/Scripts/zaibuyidao Scripts/Modules/ReaGBA`，包含 `config/`、`cache/`、`saves/`、`screenshots/`、`states/` 和默认游戏目录 `roms/`。`config/ui.json` 由前端通过 ReaWebAPI 文件服务保存。窗口位置、停靠及 WebView profile 使用 ReaWebAPI 的持久化机制，不再访问旧 `window.json`。
