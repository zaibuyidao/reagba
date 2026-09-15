# 源码构建与 ReaPack 发布

两个仓库各自独立：

| 本地目录 | 用途 | 提交内容 |
| --- | --- | --- |
| `C:/Users/ito/Documents/GitHub/reagba` | 私有源码、编辑界面、GitHub Actions 构建 | src、ui、scripts、tests、docs、许可证、CMake 和工作流 |
| `C:/Users/ito/Documents/GitHub/ReaScripts/ReaGBA` | 公开 ReaPack 发布包 | extension、web、ReaGBA.ext |

源码仓库不依赖 ReaScripts 的检出目录。公开包和已安装扩展不读取源码仓库，也不访问私有 GitHub 链接。工作流只在当前源码仓库生成 Release，不推送或更新 ReaScripts。

不要向源码仓库添加 ROM、BIOS、data、verification、third_party、build、dist、浏览器缓存或本地测试截图。修改界面使用 `ui/index.html`、`ui/style.css`、`ui/app.js`；三者必须可以直接一起运行，构建只会原样复制它们。

## 私有仓库构建

1. 修改 `CMakeLists.txt` 的 `project(ReaGBA VERSION x.y.z ...)`。
2. 自行提交并推送 main。
3. 等待五个平台全部编译、CTest 和打包通过，再从该私有仓库的 Release 下载文件。

工作流 `.github/workflows/build-native.yml` 构建 Windows x64、macOS Intel / Apple Silicon、Linux x86_64 / aarch64。使用的标准 runner 支持私有仓库，见 [GitHub runner 文档](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)。macOS 最低部署版本 12；Linux 使用 Ubuntu 24.04。

Release 包含五个平台安装 ZIP、五个原生扩展、两个 Linux 界面辅助程序、一个 `ReaGBA-ReaPack-vX.Y.Z.zip` 和 `SHA256SUMS.txt`。维护者下载需要私有仓库权限；这些链接不会写入 `.ext`。

每个平台 ZIP 含版本、平台、操作名称/ID 和文件 SHA-256 清单。汇总时验证五个平台完整、版本一致、公共 web 文件一致、文件校验和正确；失败时不发布。安装包和 ReaPack 发布包按白名单收集，不含 Lua 启动器、开发验证 EXE 或用户数据。

也可推送与 CMake 版本一致的 vX.Y.Z 标签，或在 main 上手动运行。普通分支和 PR 只保留平台构建产物。相同版本不覆盖已发布文件；中断草稿只允许从相同 commit 恢复。需要新包时递增版本号。

## 手动整理公开包

下载并解压 **ReaGBA-ReaPack-vX.Y.Z.zip**，将其中的 `ReaGBA` 内容放入公开仓库的 `ReaScripts/ReaGBA`：

```text
ReaScripts/ReaGBA/
  ReaGBA.ext
  extension/
    reaper_reagba-x64.dll
    reaper_reagba-x86_64.dylib
    reaper_reagba-arm64.dylib
    reaper_reagba-x86_64.so
    reaper_reagba-aarch64.so
    reagba-webview-x86_64
    reagba-webview-aarch64
  web/
    app.js
    index.html
    style.css
```

同一版本的 `.ext`、5 个平台扩展、2 个 Linux 辅助程序和 3 个 web 文件必须一起整理提交。最终目录除 `.ext` 外正好是 7 个 `extension` 文件和 `app.js`、`index.html`、`style.css`；不再发布 README、第三方声明或 licenses 到 `web`。Linux ZIP 保存可执行位，扩展也会在需要时给自己的辅助程序补执行权限。

`.ext` 由 `scripts/reapack.py` 自动生成。它的下载地址为 `https://raw.githubusercontent.com/zaibuyidao/ReaScripts/$commit/ReaGBA/...`；索引器将 `$commit` 替换为**公开 ReaScripts 仓库的提交**，不会使用私有源码仓库的提交或令牌。每个版本因此固定到对应公开文件。不要手动把它改成私有 Release URL。规则见 [ReaPack 官方打包文档](https://github.com/cfillion/reapack-index/wiki/Packaging-Documentation)。

由维护者使用 ReaScripts 现有发布流程生成/更新 ReaPack 索引并提交推送。此源码工作流不执行该步骤。`ReaGBA.ext` 自身是包描述，不是 Lua 脚本，也不会被添加到操作列表。

## 用户安装后的位置

ReaPack 根据每条 source 的类型和平台安装文件，公开仓库的目录结构不等于用户磁盘上的安装结构：

```text
REAPER resource directory/
  UserPlugins/reaper_reagba-<architecture>.<dll|dylib|so>
  Scripts/zaibuyidao Scripts/ReaGBA/
    web/app.js
    web/index.html
    web/style.css
    extension/reagba-webview-<architecture>  # Linux only
    ROM/                                     # default ROM folder, never packaged
    config/ saves/ states/ screenshots/ cache/ WebViewData/
```

`.ext` 的 extension 条目使用文件名，安装到 `UserPlugins` 根目录；web 和 Linux helper 条目使用 `script nomain` 并保留子目录。安装后重启 REAPER，操作列表中的 **zaibuyidao: ReaGBA** 由原生扩展直接注册，命令 ID `_REAGBA_SHOW`。没有 Lua 启动文件。

运行数据固定保存在 `Scripts/zaibuyidao Scripts/ReaGBA`，不会探测或迁移旧地址。升级不要删除用户 ROM 和存档。独立平台 ZIP 额外包含 `web/manifest.json`，ReaPack 包不需要该开发校验清单。

macOS 签名、公证和公开 ReaPack 索引需由维护者按发布流程处理。依赖的固定版本和许可证见 THIRD_PARTY_NOTICES.md；本项目自身的授权条款由仓库所有者确定。
