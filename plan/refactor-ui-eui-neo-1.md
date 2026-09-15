---
goal: 键盘热力统计 UI 现代化：基于 EUI-NEO 框架重写界面，设计令牌源自 ui-ux-pro-max-skill
version: 1.0
date_created: 2026-09-10
last_updated: 2026-09-10
owner: weilantianhai
status: 'Completed'
tags: [refactor, ui, win32, opengl, design-system]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

将 KeyboardStats 的 Win32/GDI 自绘界面重写为 **EUI-NEO** 框架（C++17 声明式 DSL + OpenGL 渲染 + 动画系统），设计令牌（色板/版式/动效）由 **ui-ux-pro-max-skill** 的设计系统生成器产出（Data-Dense Dashboard 风格）。数据层（键盘钩子、事件存储、时段查询）与托盘/自启动/单实例行为全部保留。

**已验证事实**（Phase 0 实测）：
- EUI-NEO 在 VS CMake 3.31.6 + MinGW g++ 14.2 下完整编译，产出 `kbstats_poc.exe` 3.1 MB（含中文文本渲染、托盘配置）。
- 框架内置关窗隐藏到托盘、托盘 Show/Exit 菜单（`core/app/glfw_app_main.cpp` L458-506）、`main()` 入口（应用只实现 `dslAppConfig()` + `compose()`）。
- 框架自带中文字体（assets 下 2MB/1.4MB ttf）、Font Awesome 图标、全部第三方源码（glfw/freetype/glad/zlib 等），离线构建。
- uiux-pro-max 搜索引擎可用（anaconda python 3.14），已生成本项目设计系统。

## 1. Requirements & Constraints

- **REQ-001**: 保留全部现有行为：后台记录、托盘常驻（关窗隐藏/Show/Exit）、开机自启动、104 键热力图（计数+颜色）、Top 10、直方图、时间段选择（今天/7天/30天/全部/自定义日期段）、单实例互斥
- **REQ-002**: 保持单文件 exe（静态/自包含；体积 ≤8 MB，PoC 实测 3.1 MB）
- **REQ-003**: 深色主题为主，色板采用 uiux-pro-max 生成结果（Primary `#1E40AF` / Secondary `#3B82F6` / Accent `#D97706`），热力色阶保留 蓝→黄→红 序列
- **REQ-004**: 新 UI 代码集中在单文件 `src/app.cpp`（声明式 DSL，保持用户可读的 C++）
- **REQ-005**: 数据层零行为改动：`src/hook.cpp`、`src/storage.cpp`、`src/timeutil.cpp`、`src/layout.h` 接口不变，`tests/test_query.cpp` 继续通过
- **SEC-001**: 不新增系统权限面：仅保留 HKCU Run 自启动注册表写入；程序不做任何网络通信
- **CON-001**: 工具链固定：VS2022 自带 CMake 3.31.6 + MinGW-w64 g++ 14.2 + mingw32-make（均已实测）
- **CON-002**: EUI-NEO 以本地 vendor 形式集成于 `.vendor/eui-neo`（已克隆），构建期不联网拉取任何依赖
- **CON-003**: 渲染依赖系统 OpenGL（`opengl32.dll`，Windows 自带）；目标系统 Windows 10+
- **GUD-001**: Karpathy 原则：分阶段独立验证；UI 重写不触碰数据层；不"顺手"改无关代码
- **GUD-002**: 设计令牌（颜色/字号/圆角/间距/动效时长）集中在 `src/app.cpp` 顶部常量区，映射自 uiux-pro-max 输出，禁止散落魔数
- **GUD-003**: 遵循 uiux-pro-max 交付检查清单适用项：文字对比度 ≥4.5:1、悬停过渡 150–300ms、禁用 emoji 图标（用 Font Awesome，框架自带图标字体）
- **PAT-001**: 声明式 compose 模式：`app::compose(eui::Ui&, const eui::Screen&)` 每帧重建 UI 树，状态用 `ui.state<T>()` / `eui::Signal<T>` 绑定（参照 `.vendor/eui-neo/apps/sidebar_starter/app.cpp`）
- **PAT-002**: 生命周期挂接：`app::initialize()` 初始化互斥/存储/钩子；`app::update(deltaSeconds)` 累计 ≥5s 触发落盘；`app::shutdown()` 清理钩子并强制落盘

## 2. Implementation Steps

### Implementation Phase 0: 工具链验证（已完成）

- GOAL-001: 验证 CMake + MinGW + EUI-NEO 可在无网络环境完整构建

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | 克隆 EUI-NEO 至 `.vendor/eui-neo`、ui-ux-pro-max-skill 至 `.vendor/uiux-pro-max`（git clone --depth 1，openssl TLS 后端） | ✅ | 2026-09-10 |
| TASK-002 | `.poc/CMakeLists.txt` + `.poc/main.cpp` 最小工程，`cmake -G "MinGW Makefiles"` 配置成功 | ✅ | 2026-09-10 |
| TASK-003 | PoC 编译产出 `kbstats_poc.exe`（3.1 MB，修正托盘 setter 为 `.tray(true)`） | ✅ | 2026-09-10 |

### Implementation Phase 1: 工程集成

- GOAL-002: KeyboardStats 主工程切换到 CMake 构建，EUI 窗口骨架运行起来，数据层接入框架生命周期

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-004 | 新增根 `CMakeLists.txt`：`project(KeyboardStats)`；`add_subdirectory(.vendor/eui-neo)`；`add_executable(KeyboardStats src/app.cpp src/hook.cpp src/storage.cpp src/timeutil.cpp src/win/autostart.cpp)`；`eui_neo_configure_app(KeyboardStats)`；复制 `assets/icon.png` 与中文字体到输出目录（框架按相对路径加载） | | |
| TASK-005 | 新增 `src/app.cpp` 骨架：`dslAppConfig()`（`.title("KeyboardStats 键盘热力统计")`、`.windowSize(1000,680)`、`.tray(true)`、`.trayTitle("KeyboardStats")`、`.textFont(<中文 ttf>)`）；`compose()` 先渲染占位页；`initialize()` 实现单实例互斥（`CreateMutexW("KeyboardStats.SingleInstance")`，已存在则退出）+ `StorageInit()` + `InstallHook()`；`update()` 累计 deltaSeconds ≥5.0 调 `StorageFlushIfDue()`；`shutdown()` 调 `RemoveHook()` + `StorageFlushNow()`。验证：编译运行，托盘出现，打字后事件文件增长 | | |
| TASK-006 | 自启动逻辑迁移：从旧 `src/gui.cpp` 平移 `ExePath()/AutostartEnabled()/AutostartSet()` 到 `src/win/autostart.cpp`（实现 `src/app.h` 既有声明）；重编 `tests/test_autostart.cpp` 链接新文件跑 PASS | | |
| TASK-007 | 退役旧 UI：删除 `src/gui.cpp`、`src/main.cpp`（wWinMain 与框架 main 冲突）；`build.ps1` 改为薄封装：定位 VS cmake → `cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release` → `cmake --build build --parallel`。验证：`.\build.ps1` 一键出 exe | | |

### Implementation Phase 2: 设计令牌与主题

- GOAL-003: uiux-pro-max 色板落地为代码令牌，主窗口呈现 Data-Dense Dashboard 深色视觉

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-008 | `src/app.cpp` 顶部令牌区：`namespace tokens { constexpr Color kPrimary{0x1E,0x40,0xAF}; kSecondary{0x3B,0x82,0xF6}; kAccent{0xD9,0x77,0x06}; ... }`（uiux 色板 RGB 十六进制展开）；以 `components::theme::dark()` 为基底，覆盖 primary/secondary/accent/border/muted 字段构造本项目主题函数 `ThemeColorTokens appTheme()`（参照 sidebar_starter `theme()` L371-378 模式） | | |
| TASK-009 | 版式令牌：中文正文用框架默认字体（JingNan），数字/键帽用等宽风格（框架内置或系统回退）；`RadiusTokens`：卡片 12、键帽 8、分段控件 8；间距基线 4/8/12/16 | | |
| TASK-010 | compose 顶层布局：页头（标题 + "共 N 次按键" 副标题）→ `components::segmented`（页面切换：热力图 / 直方图，选中态绑定 `ui.state<int>`）→ 内容区 `components::panel` 圆角卡片 | | |

### Implementation Phase 3: 热力图页

- GOAL-004: 104 键热力网格 + 图例 + Top 10，随打字实时变色

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-011 | 键网格：`ui.row/ui.column` 网格或绝对定位，逐键渲染（复用 `src/layout.h` 的 104 键 u 坐标 × 缩放系数）；每键 = 圆角矩形（`ui.rect` + radius 8，若 rect 无 radius 则用 `components::card` 降级）+ 底部计数文本叠加；颜色 = `heatColor(count/maxCount)` 蓝→黄→红序列（移植旧 `HeatColor()` 到 app.cpp 令牌区） | | |
| TASK-012 | 键悬停：`components::tooltip` 或 mousearea 显示"键名 · 次数"（`StatName()` 复用）；空键灰、非零键按热度着色 | | |
| TASK-013 | 图例：8 段渐变条 + "少/多"标签（ui.rect 序列） | | |
| TASK-014 | Top 10 卡片：`components::card` + 排名列表（计数降序取 10，复用现有排序逻辑于 QueryRange 结果）；`app::update` 中 QueryRange 重算节流（≥250ms 间隔或 storage 版本号变化） | | |

### Implementation Phase 4: 直方图页与时间段

- GOAL-005: 时间段选择（含自定义日期段）与直方图页，随选择实时重算

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-015 | 时间段选择：`components::segmented`（今天/最近7天/最近30天/全部）绑定 `ui.state<int>`；自定义段：`components::datepicker` 两个日期 + 应用按钮，映射 `QueryRange(4, from, to)`；范围状态存 `ui.state`，compose 内调用 `QueryRange()` | | |
| TASK-016 | 直方图：优先 `components::barchart`（喂入 `RangeStats::buckets`；实现时核对组件 API，不匹配则用 ui.rect 自绘柱 + tooltip 数值）；桶标签直接使用 storage 生成的 label | | |
| TASK-017 | 动效：所有可交互元素挂 `eui::Transition::make(0.18f, eui::Ease::OutCubic)`（符合 uiux 150-300ms 指南）；页面/时间段切换时数值与柱高平滑过渡（`.animate(AnimProperty::...)`） | | |

### Implementation Phase 5: 验证与收尾

- GOAL-006: 全量回归 + 文档更新

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-018 | 数据层回归：重编译 `tests/test_query.cpp`（链接未改动的 storage/timeutil）输出 PASS；`tests/test_autostart.cpp` 链接 `src/win/autostart.cpp` PASS | | |
| TASK-019 | E2E 手册执行：SendKeys 打字 → 热力图变色计数增加 → 切时间段 → 直方图显示 → 重启保留 → 托盘 Show/Exit → 自启动注册表写入/删除（`reg query HKCU\...\Run /v KeyboardStats`） | | |
| TASK-020 | README.md 更新：CMake 构建方式、新架构图（app.cpp 组合 EUI-NEO + 保留的数据层）、数据/隐私说明、已知限制（OpenGL 依赖） | | |
| TASK-021 | 清理：`.vendor/emil-skills` 保留但标注搁置（用户指示暂不用）；`.poc/` 保留作工具链参考；确认 `.test-data/` 是否保留由用户定 | | |

## 3. Alternatives

- **ALT-001**: 保留 Win32/GDI 自绘并做扁平化现代化（深色 + DWM 圆角 + 全自绘控件）——零新依赖但动效/组件/文本渲染全要手写，天花板低；用户已选定 EUI-NEO 方向
- **ALT-002**: WebView2 混合 UI（web 技术栈画界面）——视觉上限最高，但引入 WebView2 运行时依赖 + 进程模型复杂化，违背单文件轻量约束
- **ALT-003**: Qt Widgets/QML——组件成熟，但 LGPL/商业许可与 ~50MB 体积不可接受
- **ALT-004**: emil-skills（Emil Kowalski 动画/设计方法论）——用户指示暂不采用；仓库保留于 `.vendor/emil-skills`，其动效原则（spring/时长/缓动）日后可叠加
- **ALT-005**: 仅升级现有 GDI 版本视觉样式（manifest v6 + DWM 深色标题栏）——改动最小但提升有限，作为已放弃的保守路线记录

## 4. Dependencies

- **DEP-001**: EUI-NEO（Apache-2.0）——本地 vendor 于 `.vendor/eui-neo`（git 已克隆，commit 固定于克隆时点）；自带 glfw/freetype/glad/zlib/libpng/md4c/yyjson/tray 全部源码，构建离线
- **DEP-002**: VS2022 自带 CMake 3.31.6（`D:\Program Files\Microsoft Visual Studio\2022\Enterprise\...\cmake.exe`）+ MinGW-w64 g++ 14.2 + mingw32-make（PATH 已含）
- **DEP-003**: 系统 OpenGL 运行时（`opengl32.dll`，Windows 自带）
- **DEP-004**: anaconda Python 3.14（`C:\Users\weilantianhai\anaconda3\python.exe`）——仅开发期运行 uiux-pro-max 搜索引擎，不进构建链

## 5. Files

- **FILE-001**: `CMakeLists.txt`（新增，根工程，见 TASK-004）
- **FILE-002**: `src/app.cpp`（新增，UI 主体 + 生命周期挂接，见 TASK-005/008-017）
- **FILE-003**: `src/win/autostart.cpp`（新增，自启动注册表逻辑迁移，见 TASK-006）
- **FILE-004**: `src/gui.cpp`、`src/main.cpp`、旧 `src/app.h`（删除退役，见 TASK-007；`AutostartEnabled/AutostartSet/ExePath` 声明移入 `src/win/autostart.h`）
- **FILE-005**: `build.ps1`（改写为 CMake 封装，见 TASK-007）
- **FILE-006**: `src/hook.cpp`、`src/storage.cpp`、`src/timeutil.cpp`、`src/layout.h`（保留不动；仅当 storage 需要暴露"数据版本号"供 TASK-014 节流时允许最小增量）
- **FILE-007**: `README.md`（更新，见 TASK-020）
- **FILE-008**: `.vendor/eui-neo/`、`.vendor/uiux-pro-max/`、`.vendor/emil-skills/`（第三方参考仓库，不入库构建的只有 eui-neo）

## 6. Testing

- **TEST-001**: 数据层回归：`tests/test_query.cpp` 编译运行，断言输出 PASS（时段聚合/计数不变）
- **TEST-002**: 自启动回归：`tests/test_autostart.cpp` 对接 `src/win/autostart.cpp` 后 PASS（写→读→删 HKCU Run）
- **TEST-003**: 工具链冒烟：`.poc/` 构建 kbstats_poc.exe 成功（已完成，3.1 MB）
- **TEST-004**: E2E 手册清单（TASK-019 全项），其中打字模拟沿用 SendKeys 流程、像素/截图核对面板渲染
- **TEST-005**: 基线指标：exe ≤8 MB；冷启动到窗口可见 ≤2s；空闲 CPU ≈0%（帧循环 + 垂直同步由框架管理）

## 7. Risks & Assumptions

- **RISK-001**: 远程桌面/无 GPU 虚拟机下 OpenGL 上下文可能受限——框架支持 SDL2/Vulkan 后端切换（CMake 选项），作为降级路径
- **RISK-002**: `ui.rect` 的圆角/文本叠加/`barchart` 具体 API 与计划假设可能有出入——TASK-011/016 首步先核对组件头文件并允许等效替换（card/自绘柱），不影响数据层
- **RISK-003**: 自启动静默启动（`--background` 不弹窗）依赖框架隐藏窗口的 API（`eui::window` 层），未在 PoC 验证——降级路径：自启动时窗口正常显示（可接受）或最小化
- **RISK-004**: exe 体积从 319KB 增至 ~3-5MB（OpenGL 渲染框架静态链接）——已由 REQ-002 接受
- **ASSUMPTION-001**: 用户接受深色为主的仪表盘视觉（uiux 推荐 Data-Dense Dashboard 双模式，本计划先做深色）
- **ASSUMPTION-002**: EUI-NEO 以本地 vendor 固定版本使用，不主动追上游更新
- **ASSUMPTION-003**: 保留数据目录 `%APPDATA%\KeyboardStats\` 与 `KEYBOARDSTATS_DIR` 覆盖机制不变

## 8. Related Specifications / Further Reading

- EUI-NEO 文档：`.vendor/eui-neo/docs/`（DSL.md、组件.md、状态.md、动画.md、布局.md、集成指南.md、渲染后端架构.md）
- EUI-NEO 应用模板：`.vendor/eui-neo/apps/sidebar_starter/app.cpp`（主题/Transition/Signal 用法范例）
- ui-ux-pro-max-skill：`.vendor/uiux-pro-max/README.zh.md`（设计系统生成器与交付检查清单）；搜索引擎：`python src/ui-ux-pro-max/scripts/search.py "<query>" --domain <domain>`
- 本项目数据层说明：`README.md`（数据格式与隐私章节继续有效）
