# 多语言界面维护

ReaGBA 首次启动固定使用英文，不读取系统或浏览器语言。设置顶部的 Language / 界面语言选项支持 English、简体中文、正體中文、日本語、한국어、Español、Deutsch、Français。切换立即生效，不重新加载 ROM；搜索内容、存档槽位和未提交的输入保留。

## 资源与代码

- `ui/i18n.js`：唯一的语言资源与格式化入口。`catalogs` 用 BCP 47 语言标识作为键，`name` 使用语言自己的名称，`messages` 保存全部界面文案。
- `ui/index.html`：静态元素通过 `data-i18n` 标记；提示、无障碍名称和输入提示分别使用 `data-i18n-title`、`data-i18n-aria-label`、`data-i18n-placeholder`。HTML 中保留可直接阅读的英文初始文案。
- `ui/app.js`：动态文案调用 `t(key, values)`，状态、游戏卡片、存档时间、错误提示和文件选择器标题随语言切换更新。排序、大小写搜索、数字和日期采用当前语言的地区规则。
- `ui/style.css`：为较长标签提供换行空间。详细列表的游玩按钮占独立一行，避免遮住元数据；紧凑列表保留单行标题。

翻译只作为纯文本写入 DOM，不使用 `innerHTML`。游戏标题、路径、ROM 内容和 BIOS 不会被翻译。GBA 的 A/B、L/R、Start/Select 标签、着色器预设名称和引擎键名保持不变。

## 修改或增加语言

修改文案直接编辑 `messages` 中对应的值。新增语言时，在 `catalogs` 中复制英文对象，改为有效的语言标识，例如 `pt-BR`，填写 `name: 'Português (Brasil)'`，再逐条调整 `messages`。下拉列表由资源表自动生成，无需修改应用逻辑、HTML、打包清单或原生扩展。

保留所有语义键及占位符，例如 `{slot}`、`{title}`、`{count}`；句子中的占位符可以按目标语言的语序移动。避免把完整句子拆成词语拼接。按钮用当地软件和模拟器中常见的短语，说明文字按阅读习惯改写。品牌和硬件名称不必机械翻译。

英文是回退语言：单条译文缺失时使用英文；保存的语言不在当前资源表中时，界面使用英文，但不会仅因打开界面就覆盖已保存的语言。运行时允许未完成的语言资源，发布前的完整性测试要求所有语言拥有相同的键和占位符。加入语言后，需要同步 `tests/i18n_catalogs.cjs` 中的预期语言列表。

## 保存和兼容性

语言通过现有 `get_settings` / `set_settings` 协议读写，字段为 `language`，存入 REAPER 资源目录下的 `Scripts/zaibuyidao Scripts/ReaGBA/config/preferences.json`。不依赖浏览器本地存储，Linux 的 WebView 禁用 localStorage 也可以记住设置。

本次首次加入多语言需要更新原生扩展：旧扩展的设置白名单会丢弃 `language`。新扩展只校验语言标识的长度和字符，不限定语言列表，之后添加语言不需要重新编译扩展。界面会检查保存结果；写入失败或旧扩展忽略字段时会回退到此前的语言并提示错误。

`open_rom` 和 `select_rom_directory` 请求可附带当前语言的 `dialog_title`；原生端保留英文默认值以兼容旧界面。系统文件选择器的按钮和其他系统文字由操作系统 / REAPER 决定。常见 ROM、BIOS、存档错误在界面中本地化，未识别的底层错误保留原始诊断信息；界面加载之前的原生启动错误仍使用英文。

分发时四个文件必须齐全：`index.html`、`style.css`、`i18n.js`、`app.js`。`prepare_ui.py`、CMake 安装规则和 ReaPack 清单均包含语言资源，不需要联网或构建前端资源。

## 验证

```sh
node tests/i18n_catalogs.cjs
node tests/i18n_verify.cjs
node tests/settings_verify.cjs
node tests/library_verify.cjs
node tests/layout_verify.cjs
ctest --test-dir build/native -C Release --output-on-failure
```

浏览器测试需要 Playwright 和 Chromium；可用 `REAGBA_NODE_MODULES` 指定依赖位置，`REAGBA_BROWSER_CHANNEL=msedge` 使用本机 Edge。多语言测试禁用 localStorage，使用独立模拟原生桥验证保存、重新打开、失败回退、未知语言回退、文件选择器标题、错误提示和六种窗口尺寸；不会操作用户的 REAPER 配置。原生测试实际写入临时配置并重建 EmulatorManager，验证默认英文和语言设置持久化。

Windows 本地测试安装包位于 `build/i18n-update`，沿用当前开发版本号，未发布。正式发布前由维护者递增版本号，完成五平台构建，并在 REAPER 实机测试。不要将旧 macOS/Linux 扩展与新界面组合成正式发布包。
