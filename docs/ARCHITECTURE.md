# 架构与接口

## 职责

```text
web/zaibuyidao_ReaGBA.lua → ReaWebAPI WebView
  ├─ reaper.host.service("reagba") → Native control / atomic input → mGBA
  └─ reaper.stream.open("reagba.video") ← independent binary transport ← emulator thread
```

ReaGBA 核心扩展不链接任何浏览器 SDK，不创建窗口，不注册 command_id、gaccel、hookcommand 或 hwnd_info。扩展的 timer 负责服务延迟注册、游戏手柄轮询、输入超时、音频输出切换与目标跟踪，以及控制回复缓存过期。`src/bridge/CoreCommands` 是独立于 UI 的核心命令分发器。

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

主页面先调用 `attach({owner:true})` 创建或重新连接窗口所属的核心；独立游戏页使用 `attach({owner:false})`，不能创建核心。主线程通过 `ReaWeb_IsOpen` 检查主窗口，窗口 X、停靠分页 X 或原生窗口消失后停止线程、完成电池存档并释放音频与帧流，不依赖页面卸载请求。页面刷新保留同一窗口的核心，独立游戏页关闭不销毁主窗口核心。关闭后的状态轮询和输入不能重建核心。旧 ReaScript 创建的无界面会话仍由 `ReaGBA_Destroy` 显式销毁。

`reagba` 服务支持 `loadRom`、`closeRom`、`pause`、`resume`、`reset`、`saveState`、`loadState`、`getState`、`setSpeed`、`settings`，也接受原有 CoreCommands 的 action 名称。普通命令异步执行。大型封面或游戏库结果通过有界分块读取，保留原有 8 MiB 结果上限。每片最多 128 KiB，缓存最多四项和 16 MiB，绑定请求窗口，60 秒后过期。帧流不使用此控制回复通道。

`service.send('input', {mask, fast, active})` 在主线程直接更新原子输入状态，不排入 ROM/存档命令队列。失焦或清理页面时释放输入，活跃页面每 200 ms 刷新输入有效期。750 ms 无刷新时自动释放。

`reagba.video` 为 240×160、RGBA8、960-byte stride 的 Frame Stream。模拟线程发布完整二进制帧，使用三槽有界缓冲。页面卡顿时保留最新帧，WebGL 直接接收 Uint8Array。消费者关闭只解除自己的连接，生产者关闭时通知所有页面。同一窗口刷新或重新连接画面时可以读取暂停前的最后一帧；关闭主窗口后重新打开会创建空闲核心。

`index.html` 与独立游戏页 `game.html` 共用 `bridge.js` 和 `video.js`。`popout.js` 使用同源存储中的窗口 ID 与短期心跳协调显示和输入归属。主窗口收回画面或关闭后，独立页自动关闭。独立页退出或心跳失效时主页面恢复画面，两者不创建额外核心会话。

界面保留原有语言和控制。显示偏好使用 `config/ui.json`，核心维护模拟、ROM、封面和存档配置。对话框、停靠和聚焦复用 ReaWebAPI。

ReaWebAPI 或 ReaGBA 卸载时，服务关闭回调先停止并等待模拟线程，再关闭流和释放会话。ReaGBA 主动卸载时随后注销服务。旧 ReaScript 调用不要求安装 ReaWebAPI。

## 画面、音频与持久化

mGBA 在工作线程独占运行，像素发布至原生流并保留旧核心 API 的三缓冲，PCM 通过 SPSC 缓冲交给唯一的活动音频消费者。读取画面不阻塞模拟线程，截图仍是未经 Shader 处理的核心 BMP。

`audio_output` 接受 `system`（默认）、`reaper_output`、`reaper_track`。`audio_track` 接受 `preview`（默认）或 `selected`，由已有 `set_settings` / `setSettings` 保存至 `config/preferences.json`。`preview` 按轨道顺序查找精确名称 `ReaGBA Preview`，没有同名轨道时回退至 `GetSelectedTrack(project, 0)`。`selected` 直接跟随第一条选中轨道。没有目标时静音。主线程重新解析目标，不持久化轨道指针，不修改工程轨道或 FX。

系统默认设备（System default device）使用 SDL 默认 S16 立体声设备。REAPER 硬件输出（REAPER hardware output）使用 `PlayPreviewEx`，绕过 Master FX 与 Master 推子。`audio_channel` 是当前设备中从零开始的首通道序号（0–1023，默认 0），`audio_mono` 是单声道标志（默认 false）。立体声使用该通道及下一通道，单声道使用 `m_out_chan` 的 1024 标志。通道数量和名称来自 `GetNumAudioOutputs` / `GetOutputChannelName`。配置随 `set_settings` 保存，不绑定设备名称。通道缺失时保持静音，不回退至其他通道。设备停止、恢复或格式变化时主线程重新注册预听。

REAPER 轨道（REAPER track）使用 `PlayTrackPreview2Ex` 在目标轨道的 FX 之前注入。两种 REAPER 模式均关闭 REAPER 源预缓冲，实时 `PCM_source::GetSamples` 拉取 PCM，保留 ReaGBA 音量。三种模式共享线性重采样器，按缓冲水位的低通结果进行 PI 速率校正，修正范围为 ±0.5%，不改变模拟器帧时钟。启动和欠载后预缓冲约 47 ms 加一个输出块，队列上限为 500 ms。消费者在积压明显超过目标时丢弃旧样本并重新同步，超出校正范围的持续过载仍可能产生断点。音频回调不分配内存，不调用 UI、Lua 或 Native Service。欠载补零，暂停及长时间停止拉取后丢弃旧 PCM。切换和销毁在停止旧消费者后重置队列。

扩展适配层新增 `get_audio_outputs`，返回 `{reaper_available,reaper_device,status}`。`reaper_device` 包含 `running`、按索引排列的通道名 `channels`，以及 `GetAudioDeviceInfo` 的字符串属性 `MODE`、`IDENT_OUT`、`SRATE`、`BSIZE`。`status` 包含 `audio_output`、`audio_track`、`audio_channel`、`audio_mono`、`audio_device`、`audio_error`、`audio_state` 和 `audio_outputs_revision`。状态码为 `active`、`no_track`、`engine_stopped`、`channel_unavailable` 或 `unavailable`。设备清单变化时递增 revision，设置页据此刷新。这些状态字段也附加到模拟器状态回复。`audio_device` 表示设备或预听注册是否成功，不代表目标轨道未静音或物理设备正在发声。无目标轨道是正常静音状态。打开设备或注册失败会报告错误并定期重试，不自动切换输出模式。旧七个 ReaScript 导出与已有命令结构保持不变。PCM 不经过 ReaWebAPI Stream，设置沿用 Native Service。

WebView 使用 WebGL2 显示、整数缩放及原有 LCD 效果公式。参考公式与原生 D3D 数学验证只保留于 `tests/reference/video`，不链接进扩展。没有 WebGL2 时使用 Canvas 2D 与相同的 CPU Shader 公式回退显示，性能取决于窗口大小和设备。

模拟与音频保持约 59.73 Hz 的核心时钟。帧传输不依赖 Lua defer、普通服务 RPC 或 Runtime::tick()。vsync 控制是否通过 requestAnimationFrame 提交；浏览器仍负责最终合成与节流。

核心数据目录为 `<REAPER 资源目录>/Scripts/zaibuyidao Scripts/Modules/ReaGBA`，包含 `config/`、`cache/`、`saves/`、`screenshots/`、`states/` 和默认游戏目录 `roms/`。`config/ui.json` 由前端通过 ReaWebAPI 文件服务保存。窗口位置、停靠及 WebView profile 使用 ReaWebAPI 的持久化机制，不再访问旧 `window.json`。

## 手柄绑定

`web/gamepad.js` 定义手柄与键盘共用的显示顺序：A/B、A 连发/B 连发、Select/Start、Up/Down、Left/Right、L/R、加速。显示顺序与 GBA 按键位序分离，保留已有绑定对应关系。它在主设置页渲染双列手柄绑定，复用键盘映射的紧凑按钮样式。普通 A/B 与 A/B 连发分别显示绑定按钮，连发行紧随普通 A/B。连发项复用 `target: a/b` 和 `mode: turbo`，普通项使用 `mode: hold`，按目标和模式分别替换绑定，兼容已有配置。每个 GBA 操作及加速对应一个绑定按钮，点击后监听输入，成功录入时替换该目标的原有绑定。默认方向键与左摇杆绑定合并显示。关闭设置、切换到键盘录入或语言切换时取消监听。

`InputManager` 同时读取 SDL Joystick 的原始按钮、轴和方向帽，对有标准映射的设备保留 GameController 默认绑定，轴阈值为 16000。宿主 timer 向 `EmulatorManager` 传递标准输入位及原始输入码，工作线程每个游戏帧执行 `GamepadBindings` 后与键盘输入合并，对向方向同时按下时抵消。`fast_forward` 使用内部位 10，与键盘加速合并后临时覆盖为 4×，不会传入 GBA 按键寄存器。普通绑定保持按下状态，连发使用单调时钟，周期 100 ms、占空比 50%。断开连接、失焦或输入超时将原始输入清零。

`get_settings` / `set_settings` 复用 `gamepad_bindings` 字段。只读 Native Service 方法 `get_gamepad_input` 返回连接状态、设备名、实例 ID、当前原始输入码及对应的标准映射别名，供设置页录入。该方法在主线程轮询，不发送游戏输入。字段为以输入 ID 为键的对象，每项包含 `target` 和 `mode`。目标值为 `a/b/select/start/right/left/up/down/r/l/fast_forward/none`，模式为 `hold/turbo`，Turbo 仅用于 GBA A/B 目标。旧 `single` 配置及 A/B 以外的 Turbo 配置兼容读取并归一为 `hold`，保留源和目标。源 ID 支持 `button:<index>`、`axis:<index>:+/-`、`hat:<index>:1/2/4/8`，编号从 0 开始。标准默认源 ID 见 `src/input/GamepadBindings.h`。录入原始输入时，设置页清除同一实体输入的标准别名绑定，避免触发双重操作。提交该字段会替换整套绑定，缺省项补默认值，非法源、目标及模式被拒绝。未提交该字段时保留现有配置。启动时缺失或非法配置回退默认值。

例如将原始按钮 40 设为标准 A、按钮 41 设为连发 B，其余输入保留默认值：

```json
{"action":"set_settings","settings":{"gamepad_bindings":{"button:40":{"target":"a","mode":"hold"},"button:41":{"target":"b","mode":"turbo"}}}}
```

主界面与弹出游戏页共用 `web/bridge.js` 的存档快捷键处理。`Ctrl+1…9` 调用 `save_state`，`Shift+1…9` 调用 `load_state`，过滤重复按键、额外修饰键和编辑状态，请求完成前不重复提交。主页面同步选中槽位和存档标记。

键盘连发绑定使用 `turbo_keys: [A, B]` 保存，空字符串表示未绑定，旧配置缺失时补为空。`input` 消息增加可选 `turbo` 位掩码，位 0/1 表示 A/B 连发键的按住状态，缺省为 0。核心按与手柄相同的 100 ms 周期生成按下与释放，合并普通键盘和手柄输入。失焦、关闭设置及输入超时均释放连发输入，现有 `ReaGBA_SetInput` ABI 不变。
