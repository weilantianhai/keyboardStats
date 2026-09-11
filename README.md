# KeyboardStats 键盘热力统计

后台记录全系统键盘/鼠标按键次数与按下时间戳，托盘常驻；打开图形界面查看 104 键热力图、
按键直方图，并可按时间段筛选。

**C++17 + [EUI-NEO](https://github.com/sudoevolve/EUI-NEO) 声明式 UI 框架**（GLFW + OpenGL 渲染），
单文件 exe、静态链接、无运行时 DLL 依赖、不做任何网络通信。

> 📖 **完整操作说明**（界面导览 / 设置详解 / 数据管理 / 常见问题）见 **[docs/GUIDE.md](docs/GUIDE.md)**，
> 也可在软件「设置」页点「打开说明文档」，或访问 [Wiki](https://github.com/weilantianhai/keyboardStats/wiki)。

---

## 功能

### 统计与展示

- **热力图**：104 键 ANSI 布局按频次着色（灰=未用 → 热力色阶），键帽上显示次数；右侧 Top 10 排行
- **直方图**：按时段自动聚合（今天→每小时，7/30 天→每天，全部→每月）
- **时段切换**：`今天 / 7 天 / 30 天 / 全部` 分段切换，`自定义` 可选日期段
- **按键筛选**：键盘 / 鼠标 / 全部 / 分开（左右分区）
- 键盘翻页：`←` `→` `PgUp` `PgDn`
- **主页看板**（键鼠区上方）：活跃分数（键盘+鼠标+滚轮×0.1，今日）、今日键盘、
  今日鼠标、滚轮格数（今日）、使用天数、活跃天数
- **首次启动引导**：第一次打开会询问是否开启开机自启动（开启后开机自动后台记录，
  约 2 MB 托盘常驻），选过一次就不再出现

### 双进程架构：后台记录 + 按需界面

同一个 exe，两种模式：

| 模式 | 启动方式 | 职责 | 内存实测 |
|---|---|---|---|
| **记录进程** | `--record`（GUI 启动时自动拉起） | 全局钩子 + 计数 + 每 5 秒落盘 + **托盘图标** | **约 2 MB** |
| **图形界面** | 直接启动（或托盘菜单「打开」） | 查看统计，只读数据文件 | 约 146 MB |

- 启动 GUI 时若记录进程不在，会自动拉起；**关掉 GUI 后台记录继续**，托盘里
  「打开 KeyboardStats」随时唤起主窗口
- GUI 每 0.5 秒增量同步记录进程落盘的新增数据（只读文件新增部分，毫秒级）
- 点窗口 `×`：可配置为 **每次询问 / 直接关闭窗口 / 退出程序**（记在 `ui-close.txt`）；
  「关闭窗口」只退界面，「退出程序」连后台记录一起优雅退出（先落盘再收尾）
- 清除记录 / 切换数据文件夹 / 切换数据文件 / 转入文件等操作会通过事件通知记录进程同步重载
- 兜底：若记录进程拉不起来（权限等），GUI 会自己装钩子记录（老行为），功能不受影响

### 开机自启动

设置页开关，写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`（无需管理员）。
注册的命令带 `--record`，**开机只启动无界面记录进程 + 托盘图标（约 2 MB），不弹窗口**。
写入后有注册表读回核验（失败会提示，不静默）；首次启动会弹窗引导开启。

### 主题系统（独立"主题"页）

- **10 套内置配色**：深色、浅色、莫兰迪、赛博朋克、复古终端、森林、樱花、深海、日落、北欧极简
- **6 套热力渐变**：经典 蓝→黄→红、岩浆、冰川、霓虹、灰度、光谱
- **自定义主题色**：选一个主色 → 用 HSL 算法推导整套配色（背景/面板/描边/主次文本/选中色/互补强调色），
  可切深色底或浅色底
- **自定义热力色**：在取色器里选一个主色 → 生成三段色阶，并自动启用**平方根色阶**
  （低频段差异被拉开，小基数也能看出层次）
- 配色方案与热力方案**互相独立**，可自由组合

---

## 构建

### 前置

1. **CMake**（≥3.14）与 **MinGW-w64 g++**（本机在 `D:\Program Files\mingw64`，g++ 14.2）
2. **EUI-NEO 框架**：`.vendor/` 不在版本控制里，需要先自行获取：

   ```bash
   mkdir -p .vendor
   git clone --depth 1 https://github.com/sudoevolve/EUI-NEO.git .vendor/eui-neo
   ```

   > 框架自带 glfw / freetype / glad / zlib / libpng / md4c 与中文字体，构建期不联网。

### 构建

```powershell
.\build.ps1
```

脚本封装 CMake Configure + Build（MinGW Makefiles、Release），产出 `build\KeyboardStats.exe`
并自动把 `assets/` 部署到 exe 同级。

> `build.ps1` 里 CMake 与 mingw32-make 的路径是写死的，换机器需要改这两行。

### 运行

```bash
build\KeyboardStats.exe                 # 图形界面（会自动确保后台记录进程在跑）
build\KeyboardStats.exe --record        # 无界面记录进程（自启动用），只在托盘，约 2 MB
build\KeyboardStats.exe --page=3        # 调试用：直接打开指定页面（0热力图 1直方图 2设置 3主题）
build\KeyboardStats.exe --pick=2        # 调试用：启动即打开热力色取色浮层（1=主题色）
```

---

## 数据存储

### 位置

**设置目录**：`%APPDATA%\KeyboardStats`（环境变量 `KEYBOARDSTATS_DIR` 可覆盖），偏好文件固定在这里。

**数据文件夹**默认是 **`<exe 所在目录>\keyboardstats`**（不存在自动创建）。解析顺序：

1. `ui-data.txt` 的 `dir=`（在设置页选过就听它的）
2. 环境变量 `KEYBOARDSTATS_DIR`（测试/便携运行；设了就等于"整个程序的家目录"）
3. `<exe 目录>\keyboardstats`
4. 上面那条建不出来（例如程序装在 `Program Files`）→ 退回设置目录，保证程序仍可用

> 从旧版本升级时，若新位置没有数据而旧的 `%APPDATA%\KeyboardStats` 有，会**拷贝**一份过去
> （只拷贝、不移动、不删除），避免切换后看起来"统计变空了"。

### 文件

| 文件 | 说明 |
|---|---|
| `events-YYYYMM.jsonl` | 事件流，按月一个文件，**每行一条 JSON**：`{"k":65,"t":"2026-01-15T09:30:12.345"}`（k=虚拟键码 0–255，65=A；t=本地时间，毫秒精度） |
| `ui-theme.txt` | 配色方案 / 热力方案 / 自定义色：`palette=`、`heat=`、`accent=`、`heatbase=`、`mode=` |
| `ui-font.txt` | 字号偏好：`auto=0\|1`、`scale=1.25` |
| `ui-layout.txt` | 版面偏好：`kb=`、`mouse=`、`split=` |
| `ui-data.txt` | 数据位置：`dir=` 数据文件夹、`file=` 当前数据文件名（空 = 自动按月） |
| `ui-close.txt` | 关窗行为：`mode=ask\|min\|exit` |

内存缓冲批量落盘：每 5 秒或退出时写入，钩子回调不做任何磁盘 I/O。
启动时由事件流重建内存按日聚合，它**是唯一数据真相源**（`counts.json` 已废弃）。

### 数据文件标记行

每个数据文件第一行是标记行：

```json
{"format":"keyboardstats-data","version":1}
```

新建文件、首次写入某月的按月文件、导出 JSONL 时都会自动带上。识别规则：

| 情况 | 判定 |
|---|---|
| 第一行有标记行 | 数据文件 |
| 无标记，但每一行都是可识别事件 | 数据文件（兼容旧版本导出） |
| 其它（CSV、随便什么文本） | 不是数据文件，转入时拒绝并提示 |

### 自选数据位置（设置页）

- **数据文件夹** — `更改` 选任意文件夹（不存在自动创建），`默认` 回到 `<exe 目录>\keyboardstats`。
  若当前数据文件夹里有数据文件，会先弹窗问 `取消 / 仅切换 / 一起移动`。
  **切换前会做写入预检**（建临时文件再删）：目录建不出来或没有写权限（如 `C:\Program Files`、
  `C:\Windows`）时**直接拒绝并说明原因，不做任何改动**；要求搬运却一个都没成功时也会中止切换。
- **数据文件** — `新建` 创建 `data-YYYYMMDD-HHMMSS.jsonl`（带标记行）并切过去；
  `选择` 把一个 `*.jsonl` **转移**进数据文件夹并切换（校验标记，非法文件会被拒）；
  `自动` 恢复按月轮转。指定数据文件后，新记录全部追加到该文件，只读它。

**记录管理**：转入文件、导出 JSONL（带标记，可直接再转入）、导出 CSV（`time,keycode,keyname`，UTF-8 BOM）、
打开数据目录、清除全部记录（两步确认）。
鼠标与滚轮使用伪键码：左键 1、右键 2、中键 4、侧键 5/6，滚轮上/下/左/右 224/225/226/227。

> 自动模式下"清除全部记录"删除 `events-*.jsonl`；指定了数据文件时只清空内容并补回标记行。

---

## 代码结构

```
src/app.cpp            应用入口：运行状态、统计服务（500ms 刷新）、窗口最小尺寸、
                       单实例、关窗行为拦截、页面路由
src/theme.cpp          配色方案（10 套）、热力方案（6 套）、自定义色推导、偏好持久化
src/fontscale.cpp      字体/控件缩放策略（自动=跟随窗口宽度 / 自定义=设置页滑块），
                       量化 + 防抖，持久化到 ui-font.txt
src/pages.cpp          页面绘制：头部、控制行、热力图页、Top 10、直方图页、设置页、
                       主题页、按键次数直方图、关窗确认弹窗
src/recorder.cpp       无界面记录进程（--record）：钩子+落盘+托盘图标（GDI+ 生成图标），
                       托盘菜单「打开 KeyboardStats」启动/唤起 GUI，「退出」优雅收尾
src/state.h            共享运行状态与 FetchStats 声明
src/pref.h             偏好文件读写（header-only，不依赖框架，数据层也能用）
src/ui_util.h          通用小工具（颜色/编码/格式化，header-only）
src/win/autostart.cpp  开机自启动注册表逻辑
src/win/filedialog.h   打开/保存/文件夹选择对话框（header-only）
src/hook.cpp           WH_KEYBOARD_LL / WH_MOUSE_LL 低级钩子（只观察不拦截）
src/storage.cpp        数据位置解析、事件缓冲落盘、按日聚合、时段查询、记录管理
src/layout.h           104 键 ANSI 布局表 + 键名映射 + 鼠标伪键码
src/timeutil.cpp       公历日期算法（Howard Hinnant）、时间格式化
assets/icon.png        托盘/窗口图标
build.ps1              CMake 一键构建
tests/                 数据层、注册表、记录管理、默认目录解析的独立测试程序
plan/                  当初 UI 重写为 EUI-NEO 的方案存档
```

### vendored 框架补丁（`.vendor/` 不受版本控制，升级框架时需重放）

1. `core/app/glfw_app_main.cpp` → `getDpiScale()`：由 framebuffer/window 比例推导缩放，
   避免 OS 报告的缩放与真实 framebuffer 不一致导致渲染与命中判定错位。
2. `core/render/text.cpp` → 字形图集自愈：共享灰度图集只有一页且原实现无淘汰，字号持续变化
   （设置页字体滑块 / 窗口宽度自适应）会把页面写满，之后新字形永久缺失、文字错乱。
   现在写满时清空该页并递增"图集重置版本号"，`TextPrimitive::Impl::prepare()` 检测到版本
   变化即丢弃失效 UV 并重建字形。
3. `components/datepicker.h` → 滚轮列按「格」累积，**两格推进一行**，避免原实现"每个事件走一行"
   在高分辨率滚轮下飞快、而按像素阈值又慢到要滚七八格才动一行。
4. `core/app/glfw_app_main.cpp` → 拖动窗口边框时实时重绘：Windows 的模态缩放循环嵌套在
   `glfwWaitEvents` 内，主循环无法出帧。现在把「更新+渲染一帧」提取为 `renderOnce` 并通过
   `g_liveResizeRender` 暴露给 framebuffer-size / window-refresh 回调（重入保护、首帧保护、
   6ms 限频）。同时 `src/app.cpp` 在 `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` 间冻结字号缩放。

---

## 测试

```powershell
.\tests\run_tests.ps1
```

| 程序 | 覆盖 |
|---|---|
| `test_query` | 数据层回归：时段查询各模式的桶结构、每键计数 |
| `test_autostart` | 注册表自启动开关往返 |
| `test_records` | 标记识别 / 非法文件被拒 / 导出导入 / 新建 / 搬运 / 可写预检 / 切到无效文件夹被拒 |
| `test_default_dir` | 默认数据文件夹解析（不调用 `StorageInit`，不碰偏好文件） |

> `test_records` 结尾会 `StorageClearAll()`，因此**必须在隔离目录运行**：`run_tests.ps1` 会用
> `KEYBOARDSTATS_DIR` 指向临时目录，并只从真实数据目录**复制**一份种子事件，绝不写真实数据。
> 单独手动跑时请自己设好 `KEYBOARDSTATS_DIR`。

---

## 隐私

数据只存在本机。events 日志记录"什么键 + 何时"，理论上可还原打字行为，**请勿同步到云端**。
程序自身不做任何网络通信。

---

## 后续可做

- 数据文件合并：搬运时若遇重名会生成 `xxx-1.jsonl`，同一月份可能分成多个文件
  （统计会一起读，但看着不整齐）。
