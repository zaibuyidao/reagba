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

页面继续使用 `get_settings` / `set_settings` 调用方式，迁移适配器把 `language` 等显示偏好通过 ReaWebAPI 文件服务写入 `Scripts/zaibuyidao Scripts/ReaGBA/config/ui.json`。首次运行读取旧核心配置中的显示字段；核心不再校验或保存新语言配置。新增语言无需编译核心。

`open_rom` 和 `select_rom_directory` 由 ReaWebAPI 文件对话框实现，请求保留当前语言的 `dialog_title`。系统文字由操作系统 / REAPER 决定。核心错误仍由页面翻译；未识别的错误保留原始信息。

四个 Web 文件与 `Open.lua` 一起放在本仓库的 `ui/` 中，无需前端构建或修改核心包。

## 验证

```sh
node tests/i18n_catalogs.cjs
node tests/i18n_verify.cjs
node tests/settings_verify.cjs
node tests/library_verify.cjs
node tests/layout_verify.cjs
ctest --test-dir build/native -C Release --output-on-failure
```

浏览器测试需要 Playwright 和 Chromium；可用 `REAGBA_NODE_MODULES` 指定依赖位置，`REAGBA_BROWSER_CHANNEL=msedge` 使用本机 Edge。多语言测试禁用 localStorage，使用独立模拟原生桥验证保存、重新打开、失败回退、未知语言回退、文件选择器标题、错误提示和六种窗口尺寸；不会操作用户的 REAPER 配置。真实 Lua/Core/ReaWebAPI 持久化回归见 `tests/reawebapi_smoke.py`。
