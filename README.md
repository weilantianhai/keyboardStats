# KeyboardStats 键盘热力统计

后台记录全系统键盘按键次数与按下时间戳，托盘常驻，打开图形页面查看 104 键热力图与按键直方图。C++17 + [EUI-NEO](https://github.com/sudoevolve/EUI-NEO) 声明式 UI 框架（GLFW + OpenGL 渲染，Dark Dashboard 主题，设计令牌来自 ui-ux-pro-max 数据密集仪表盘方案）。

## 构建

需要 CMake + MinGW-w64 g++（本机 `D:\Program Files\mingw64`）：

```powershell
.\build.ps1
```

脚本封装 CMake Configure + Build（MinGW Makefiles，Release），产出 `build\KeyboardStats.exe`（静态链接，无运行时 DLL 依赖），并自动部署 `assets/` 到 exe 目录。

## 使用

1. 双击 `KeyboardStats.exe` → 托盘出现图标，开始后台记录
2. 单击托盘图标或右键菜单 → 打开主窗口；关闭窗口 = 隐藏到托盘
3. 主窗口（1180x720，可随窗口缩放）：
   - **`热力图 / 直方图`** 分段切换页面（也可用 ←/→ 方向键或 PgUp/PgDn 切换）
   - **`今天 / 最近7天 / 最近30天 / 全部`** 分段切换统计时段
   - **`自定义`**：展开两个日期选择器选择日期段，`应用` 生效
   - 热力图页：104 键按频次着色（灰=未用 → 蓝 → 黄 → 红），键帽显示次数，右侧 Top 10 排行
   - 直方图页：按时段聚合柱状图（今天→每小时，7/30 天→每天，全部→每月）
4. 右键托盘图标 → 退出（真正退出；退出前自动落盘）
5. 重复启动会提示"已在运行"（单实例互斥）

开机自启动：注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`（`AutostartSet`，无需管理员），可用测试程序验证。

## 数据存储

数据目录：`%APPDATA%\KeyboardStats`（可用环境变量 `KEYBOARDSTATS_DIR` 覆盖，测试与便携运行用）。

| 文件 | 说明 |
|---|---|
| `events-YYYYMM.jsonl` | 事件流，按月一个文件，**每行一条 JSON**：`{"k":65,"t":"2026-01-15T09:30:12.345"}`（k=虚拟键码 0–255，65=A；t=本地时间，毫秒精度） |
| `counts.json` | 全时段每键累计计数（由事件流推导的缓存，30 秒节流重写） |
| `ui-theme.txt` | 主题偏好：`mode=light\|dark` |
| `ui-font.txt` | 字号偏好：`auto=0\|1`、`scale=1.25` |
| `ui-layout.txt` | 版面偏好：`kb=`、`mouse=`、`split=` |

内存缓冲批量落盘：每 5 秒或退出时写入，钩子回调不做任何磁盘 I/O。

**记录管理（设置页）**：导入记录（选择 `*.jsonl`，按 `t` 的月份归入对应月度文件并重建聚合）、
导出 JSONL（原样事件，按时序）、导出 CSV（`time,keycode,keyname`，带 UTF-8 BOM）、
打开数据目录、清除全部记录（两步确认弹窗）。鼠标与滚轮使用伪键码：左键 1、右键 2、
中键 4、侧键 5/6，滚轮上/下/左/右 224/225/226/227。

**隐私**：数据只存在本机。events 日志记录"什么键 + 何时"，理论上可还原打字行为，请勿同步到云端。程序自身不做任何网络通信。

## 验证状态（2026-09-10，EUI-NEO 重写版实测）

| 项目 | 结果 |
|---|---|
| 构建链 | ✅ CMake + MinGW，单 exe 3.2 MB + assets |
| 数据层回归 | ✅ test_query：total=703，37 非零键，空格=39 A=37 Z=9 G=24 |
| 开机自启动注册表 | ✅ test_autostart：before=0 afterOn=1 afterOff=0 PASS |
| 钩子捕获 → 5 秒落盘 | ✅ 发送 4 次按键 → 事件文件 24→28 行 |
| 深色主题渲染 | ✅ 像素级抽样：背景 #0B0C10 / 面板 #141822 占比 58%，无白屏 |
| 热力图着色 | ✅ 暖色键（蓝→黄→红梯度）+ 空闲键帽灰像素签名 |
| 页签切换（热力图/直方图） | ✅ 点击触发 page onChange v=1，抓屏确认键帽消失、柱状图出现 |
| 时段切换 | ✅ 4 次范围 onChange 触发 |
| 关闭 → 托盘驻留 | ✅ WM_CLOSE 后进程存活、窗口隐藏 |
| 键盘翻页（←/→/PgUp/PgDn） | ✅ onKeyEvent 处理器已注册 |

## 代码结构

```
src/app.cpp            EUI-NEO 应用入口：运行状态、统计服务（500ms 定时刷新）、
                       窗口最小尺寸、单实例、托盘、键盘导航
src/theme.cpp          主题令牌（深/浅）、热度渐变、偏好文件读写
src/fontscale.cpp      字体/控件缩放策略（自动=跟随窗口宽度 / 自定义=设置页滑块），
                       量化 + 防抖，持久化到 ui-font.txt
src/pages.cpp          页面绘制：头部、控制行、热力图页、Top 10 侧栏、直方图页、
                       设置页、按键次数直方图（含悬浮提示）
src/state.h            共享运行状态与 FetchStats 声明
src/ui_util.h          通用小工具（颜色/编码/格式化，header-only）
src/win/autostart.cpp  开机自启动注册表逻辑
src/hook.cpp           WH_KEYBOARD_LL 低级键盘钩子（只观察不拦截）
src/storage.cpp        事件缓冲落盘、按日聚合、时段查询引擎
src/layout.h           104 键 ANSI 布局表 + 键名映射
src/timeutil.cpp       公历日期算法（Howard Hinnant）、时间格式化
assets/icon.png        托盘/窗口图标
build.ps1              CMake 一键构建
tests/                 数据层与注册表逻辑的独立测试程序
```

## vendored 框架补丁（`.vendor/` 不受版本控制，升级框架时需重放）

1. `core/app/glfw_app_main.cpp` → `getDpiScale()`：由 framebuffer/window 比例推导缩放，
   避免 OS 报告的缩放与真实 framebuffer 不一致导致渲染与命中判定错位。
2. `core/render/text.cpp` → 字形图集自愈：共享灰度图集只有一页且原实现无淘汰，字号持续
   变化（设置页字体滑块 / 窗口宽度自适应）会把页面写满，之后新字形永久缺失、文字错乱。
   现在写满时清空该页并递增“图集重置版本号”，`TextPrimitive::Impl::prepare()` 检测到版本
   变化即丢弃失效 UV 并重建字形。
3. `components/datepicker.h` → 滚轮列按「格」累积，**两格推进一行**（普通滚轮一格仅累积进度，
   第二格才走一行；高分辨率滚轮按 0.1 格/事件等比累积），避免原实现"每个事件走一行"在高分辨率
   滚轮下飞快、而按像素阈值又慢到要滚七八格才动一行。
4. `core/app/glfw_app_main.cpp` → 拖动窗口边框时实时重绘：Windows 的模态缩放循环嵌套在
   `glfwWaitEvents` 内，主循环无法出帧，原实现只在松手后更新。现在把「更新+渲染一帧」提取
   为 `renderOnce` 并通过 `g_liveResizeRender` 暴露给 framebuffer-size / window-refresh
   回调：回调发现 framebuffer 尺寸与上次渲染不同就立即出帧（重入保护、首帧保护、6ms 限频）。
   同时 `src/app.cpp` 在 `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` 间冻结字号缩放，避免拖动过程中
   每个中间宽度都触发全量字形重建。

旧纯 Win32/GDI 版本（`gui.cpp/main.cpp`）已被 EUI-NEO 版本取代并移除。
