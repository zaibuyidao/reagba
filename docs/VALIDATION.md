# Native Service / Frame Stream 验证 — 2026-09-26

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

实体手柄、商业游戏长时间运行和用户音频设备的听感未在本次自动验证中覆盖。ReaGBA 的 PCM 仍由原有 SDL 音频路径输出。内建音频分析属于 ReaWebAPI，其三平台验证记录在对应仓库。

## 复现

```sh
python scripts/build.py
ctest --test-dir build/native -C Release --output-on-failure
node tests/BridgeTests.cjs
node tests/frame_decode.cjs
node tests/reaweb_video.cjs
```

前端测试默认读取本仓库 `web/`。浏览器测试需要 Playwright，可用 `REAGBA_NODE_MODULES` 指定模块目录，`REAGBA_BROWSER_CHANNEL=msedge` 使用本机 Edge。

```sh
python tests/native_service_api.py --core build/native/bin/Release/reaper_reagba-x64.dll --rom /path/to/test.gba --output build/native-service-check
python tests/reawebapi_smoke.py --reaper C:/REAPER/reaper.exe --reawebapi ../ReaWebAPI/build/Release/reaper_reawebapi-x64.dll --rom /path/to/test.gba --audio-config C:/REAPER/reaper.ini --output build/native-ui-check
```

输出目录必须尚不存在。实机测试只创建隔离 REAPER 配置，不替换已安装扩展、日常配置或项目。`--rom` 省略时，界面测试生成一个简单的原创 ARM 测试程序。帧率与兼容性结论使用实际 Apotris ROM，不分发该 ROM。
