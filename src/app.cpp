// KeyboardStats — EUI-NEO 现代化 UI（应用入口与统计服务）
// 页面绘制见 pages.cpp，主题令牌见 theme.cpp/h，数据层零改动复用
#include "eui_neo.h"

#include "hook.h"
#include "storage.h"
#include "timeutil.h"
#include "layout.h"
#include "theme.h"
#include "state.h"
#include "pages.h"
#include "ui_util.h"
#include "fontscale.h"
#include "recorder.h"
#include "pref.h"
#include "win/adminmode.h"
#include "win/autostart.h"

#include <windows.h>
#include <exception>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace app {

// ────────────────── 运行状态定义（声明见 state.h） ──────────────────

int  g_page = 0;
int  g_debugPick = 0;
int  g_rangeMode = 3;
bool g_onboardOpen = false;
uint32_t g_customFrom = 0, g_customTo = 0;
uint32_t g_pendingFrom = 0, g_pendingTo = 0;

RangeStats g_stats;
long g_maxKey = 0;
std::vector<float> g_barVals;
std::vector<std::string> g_barLabels;
std::vector<TopEntry> g_keyHist;
std::string g_rangeText;

eui::Signal<bool> g_fromOpen{false};
eui::Signal<bool> g_toOpen{false};
int g_keyFilter = 2;   // 按键计数筛选：0=键盘 1=鼠标 2=全部 3=分开

static bool g_startHidden = false;
static bool g_uiServicesReady = false;
static UINT_PTR g_timerId = 0;
static bool g_selfRecording = false;   // 记录进程拉不起来时，GUI 兜底自己记录

// 窗口最小尺寸：启动阶段的重试计数与执行函数（实现见文件后部）
static int  s_enforceLeft = 20;   // 最多尝试次数（500ms/次）
static void EnforceMinSizeOnce();

// ────────────────── 数据获取与节流刷新 ──────────────────

static std::string RangeTitle() {
    uint32_t today = TodayLocal();
    switch (g_rangeMode) {
        case 0: return "时间段：今天 (" + Utf8(YmdToStr(today)) + ")";
        case 1: return "时间段：最近 7 天 (" + Utf8(YmdToStr(AddDays(today, -6))) + " ~ " + Utf8(YmdToStr(today)) + ")";
        case 2: return "时间段：最近 30 天 (" + Utf8(YmdToStr(AddDays(today, -29))) + " ~ " + Utf8(YmdToStr(today)) + ")";
        case 3: return "时间段：全部";
        default: {
            uint32_t a = g_customFrom, b = g_customTo;
            if (!a || !b) return "时间段：全部";
            if (a > b) std::swap(a, b);
            return "时间段：自定义 " + Utf8(YmdToStr(a)) + " ~ " + Utf8(YmdToStr(b));
        }
    }
}

void FetchStats() {
    uint32_t today = TodayLocal();
    uint32_t from = 0, to = 0;
    switch (g_rangeMode) {
        case 0: from = to = today; break;
        case 1: from = AddDays(today, -6); to = today; break;
        case 2: from = AddDays(today, -29); to = today; break;
        case 4:
            from = g_customFrom; to = g_customTo;
            if (!from || !to) { from = to = 0; }
            else if (from > to) std::swap(from, to);
            break;
        default: break;   // 3 = 全部
    }

    RangeStats s = QueryRange(g_rangeMode, from, to);

    g_maxKey = 0;
    for (int vk = 0; vk < 256; ++vk) g_maxKey = std::max(g_maxKey, s.counts[vk]);

    // barChart 组件把 values 当作 0~1 比例（内部 clamp 后乘绘图高度），因此必须传
    // "次数 / 峰值"，否则每根非零柱都被钳到 1.0，显示满高且 tooltip 恒为 100%
    long bucketMax = 0;
    for (const Bucket& b : s.buckets) bucketMax = std::max(bucketMax, b.count);
    g_barVals.clear(); g_barLabels.clear();
    // 桶数超过 15 时（如 30 天）横坐标只保留日期能被 5 整除的那天，避免文字重叠
    const bool sparseLabels = s.buckets.size() > 15;
    for (const Bucket& b : s.buckets) {
        g_barVals.push_back(bucketMax > 0 ? (float)b.count / (float)bucketMax : 0.0f);
        std::string label = Utf8(b.label);
        if (sparseLabels && label.size() >= 5 && label[2] == '-') {
            const int day = atoi(label.c_str() + 3);
            if (day <= 0 || (day % 5) != 0) label.clear();
        }
        g_barLabels.push_back(label);
    }

    // 按键分布：全键参与（含小键盘与鼠标伪键），未使用的计数为 0，升序排列。
    // 这样小键盘数字（1`、2`…）即使在未使用/NumLock 关闭时也能在直方图中被识别。
    g_keyHist.clear();
    {
        std::vector<std::tuple<long, std::string, bool>> all;
        std::vector<uint8_t> seen;
        auto add = [&](uint8_t vk) {
            for (uint8_t s : seen) if (s == vk) return;   // 主键区/小键盘的回车等重复键码
            seen.push_back(vk);
            const wchar_t* nm = StatName(vk);
            all.push_back({(vk < 256) ? s.counts[vk] : 0,
                           nm ? Utf8(nm) : ("VK" + std::to_string(vk)),
                           IsMouseKey(vk)});
        };
        for (int i = 0; i < kKeyCount; ++i) add(kKeys[i].vk);
        for (uint8_t vk : {kMouseLeft, kMouseRight, kMouseMiddle, kMouseX1, kMouseX2,
                           kWheelUp, kWheelDown, kWheelLeft, kWheelRight}) add(vk);
        long top1 = 0;
        for (const auto& kv : all) top1 = std::max(top1, std::get<0>(kv));
        std::stable_sort(all.begin(), all.end(),
                         [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
        for (const auto& kv : all) {
            g_keyHist.push_back({std::get<1>(kv), std::get<0>(kv),
                                 top1 > 0 ? (float)std::get<0>(kv) / (float)top1 : 0.0f,
                                 std::get<2>(kv)});
        }
    }

    g_stats = std::move(s);
    g_rangeText = RangeTitle();
}

static bool StatsDiffer(const RangeStats& a, const RangeStats& b) {
    if (a.total != b.total) return true;
    if (memcmp(a.counts, b.counts, sizeof(a.counts)) != 0) return true;
    if (a.buckets.size() != b.buckets.size()) return true;
    for (size_t i = 0; i < a.buckets.size(); ++i)
        if (a.buckets[i].count != b.buckets[i].count) return true;
    return false;
}

// 键鼠按下动画的驱动：框架是事件驱动渲染，按键状态变化不会自己触发重绘。
// 16ms 轮询一次共享状态，只在"最近 1 秒内有按键事件"时请求重绘（平时开销≈0）。
static void CALLBACK TickKeyAnim(HWND, UINT, UINT_PTR, DWORD) {
    if (SharedKeyAlive()) app::requestUpdate();
}

// 周期任务：驱动落盘 + 数据节流刷新（单线程）
static void CALLBACK TickTimer(HWND, UINT, UINT_PTR, DWORD) {
    StorageFlushIfDue();

    // 兜底自记录的善后：若记录进程只是启动慢（EnsureRecorderRunning 超时误判），
    // 它一起来就立刻卸掉自己的钩子，避免双钩子把同样的按键记两遍。
    if (g_selfRecording) {
        if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kIpcRecorderMutex)) {
            CloseHandle(m);
            RemoveHook();
            StorageFlushNow();     // 兜底期间的数据先落盘
            g_selfRecording = false;
            StorageReloadFull();   // 以文件为准重载（可能与记录进程有少量重复，可接受）
        }
    } else {
        // 记录进程每 5 秒落盘，GUI 这里增量同步（文件没变时开销≈0）
        StorageReloadIfChanged();
    }

    TickFontScale(GetTickCount64() / 1000.0);   // 字号滑块：值稳定后才应用

    if (s_enforceLeft > 0) { --s_enforceLeft; EnforceMinSizeOnce(); }

    RangeStats fresh = QueryRange(g_rangeMode, g_customFrom, g_customTo);   // 粗比较即可
    if (StatsDiffer(fresh, g_stats)) {
        FetchStats();
        app::requestUpdate();
    }
}

// ────────────────── 窗口最小尺寸（Win32 子类化拦截 WM_GETMINMAXINFO） ──────────────────

static WNDPROC s_origWndProc = nullptr;
static POINT   s_minTrack = {0, 0};
// 拖动窗口边框期间为 true：此时冻结字号缩放，避免每个尺寸变化都重建全部字形
// （重建 ≈ 全量光栅化 + 大图集上传，会让拖动帧率骤降）
static bool    s_liveResizing = false;

// 布局能容纳的最小客户区（控制行为固定宽度，小于此值会重叠/溢出）
static const int kMinClientW = 1140;
static const int kMinClientH = 640;

static HWND MainWindowHandle();   // 定义在下面

// ────────────────── 关闭行为（点 ×） ──────────────────
// 托盘图标属于记录进程（--record），GUI 关窗即退出自己，后台记录不受影响：
//   「关闭窗口」= 只退出图形界面，托盘里随时可以再唤起；
//   「退出程序」= 连后台记录一起退出（先通知记录进程落盘收尾）。

bool g_closeDialogOpen = false;
bool g_closeDontAsk = false;

int CurrentCloseAction() {
    const std::string v = PrefGetValue(L"ui-close.txt", "mode", "ask");
    if (v == "min") return kCloseToTray;
    if (v == "exit") return kCloseExit;
    return kCloseAsk;
}

void SetCloseAction(int mode) {
    PrefSetValue(L"ui-close.txt", "mode",
                 mode == kCloseToTray ? "min" : (mode == kCloseExit ? "exit" : "ask"));
}

void ExitAppNow() {
    // ExitProcess 不跑 atexit 回调，所以这里手动做掉它该做的事
    // （GUI 模式下钩子/缓冲只有兜底自记录时才有内容，平调无害）
    RemoveHook();
    StorageFlushNow();
    ExitProcess(0);
}

void CloseDialogDecide(bool exitApp) {
    g_closeDialogOpen = false;
    if (g_closeDontAsk) SetCloseAction(exitApp ? kCloseExit : kCloseToTray);
    if (exitApp) StorageSignalShutdown();   // 「退出程序」连后台记录一起收尾
    ExitAppNow();
}

static LRESULT CALLBACK MinSizeWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        switch (CurrentCloseAction()) {
            case kCloseToTray:   // 「关闭窗口」：退 GUI，记录进程继续
                ExitAppNow();
                return 0;
            case kCloseExit:     // 「退出程序」：连记录进程一起
                StorageSignalShutdown();
                ExitAppNow();
                return 0;
            default:
                g_closeDialogOpen = true;          // 显示应用内确认弹窗
                app::requestUpdate();
                return 0;
        }
    }

    if (msg == WM_GETMINMAXINFO && s_minTrack.x > 0) {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = s_minTrack.x;
        mmi->ptMinTrackSize.y = s_minTrack.y;
        return 0;
    }
    // 拖动/缩放期间冻结字号（松手后再按最终宽度更新一次）
    if (msg == WM_ENTERSIZEMOVE) {
        s_liveResizing = true;
    } else if (msg == WM_EXITSIZEMOVE) {
        s_liveResizing = false;
        app::requestUpdate();
    }
    return CallWindowProcW(s_origWndProc, h, msg, wp, lp);
}

// 本进程第一个带标题的可见顶层窗口（GLFW 主窗口；框架未暴露句柄）
static HWND MainWindowHandle() {
    struct Ctx { HWND found; } ctx{nullptr};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(p);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
        if (GetWindowTextLengthW(h) == 0) return TRUE;
        c->found = h;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

static void ApplyMinWindowSize() {
    HWND main = MainWindowHandle();
    if (!main) return;
    RECT wr{}, cr{};
    GetWindowRect(main, &wr);
    GetClientRect(main, &cr);

    // 不额外乘 DPI：ptMinTrackSize 与 GetClientRect 同坐标系（窗口矩形空间），
    // 乘一次 GetDeviceCaps 会让限值比实际客户区大一倍以上（已实测）
    const int clientW = kMinClientW;
    const int clientH = kMinClientH;
    s_minTrack.x = clientW + ((wr.right - wr.left) - cr.right);
    s_minTrack.y = clientH + ((wr.bottom - wr.top) - cr.bottom);
    s_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(main, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MinSizeWndProc)));
    s_enforceLeft = 40;
}

// 客户区小于下限时放大：启动后持续检查（框架会在首帧之后还原窗口尺寸，
// 达标也不能提前停止）。上限 40 次（500ms/次）后放弃，避免与窗口管理器死循环。
static void EnforceMinSizeOnce() {
    if (s_enforceLeft <= 0) return;
    --s_enforceLeft;
    HWND main = MainWindowHandle();
    if (!main || s_minTrack.x <= 0) return;
    RECT cr{}, wr{};
    GetClientRect(main, &cr);
    const bool below = (cr.right < kMinClientW || cr.bottom < kMinClientH);
    if (!below) return;
    GetWindowRect(main, &wr);
    SetWindowPos(main, nullptr, 0, 0,
                 (cr.right < kMinClientW) ? s_minTrack.x : (wr.right - wr.left),
                 (cr.bottom < kMinClientH) ? s_minTrack.y : (wr.bottom - wr.top),
                 SWP_NOMOVE | SWP_NOZORDER);
}

static void EnsureUiServices() {
    if (g_uiServicesReady) return;
    g_uiServicesReady = true;

    ApplyMinWindowSize();

    // 消息专用窗口：承载落盘/刷新定时器（与 GLFW 消息泵同线程）
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"KeyboardStatsMsgWnd";
    RegisterClassW(&wc);
    HWND msg = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                               nullptr, nullptr, nullptr);
    if (msg) {
        g_timerId = SetTimer(msg, 1, 500, TickTimer);
        SetTimer(msg, 2, 16, TickKeyAnim);   // 按下动画驱动
    }

    FetchStats();

    // 首次启动：弹出自启动引导（用户选择过一次就不再出现）
    if (PrefGetValue(L"ui-general.txt", "onboarded", "0") != "1")
        g_onboardOpen = true;

    if (g_startHidden) {
        // 自启动静默启动：框架无"初始隐藏"，退化为启动即最小化
        HWND main = GetActiveWindow();
        if (main) ShowWindow(main, SW_MINIMIZE);
    }
}

static std::string ExeDirA() {
    // 宽字符 API + Narrow(UTF-8)：libstdc++ filesystem 按 UTF-8 解释窄路径，
    // GBK 字节路径（exe 在中文目录）会在资源解析时 abort
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::string() : Narrow(p.substr(0, s + 1));
}

static std::string AssetAbs(const char* name) { return ExeDirA() + "assets\\" + name; }

// ────────────────── EUI-NEO 应用入口（框架提供 main） ──────────────────

const DslAppConfig& dslAppConfig() {
    // 入口分流/单实例/存储/钩子：必须在窗口出现前完成（静态初始化器时机最早）
    static const bool coreReady = [] {
        // 无声 abort（如路径编码 fail-fast）落盘留痕，避免"崩得毫无线索"
        std::set_terminate([] {
            const std::wstring path = app::PrefDirPath() + L"\\terminate-report.txt";
            if (FILE* f = _wfopen(path.c_str(), L"ab")) {
                SYSTEMTIME t;
                GetLocalTime(&t);
                fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u] std::terminate / fail-fast (pid %lu)\n",
                        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                        GetCurrentProcessId());
                fclose(f);
            }
            abort();
        });

        // ── 无界面记录进程：--record → 钩子 + 落盘 + 托盘，不进 GUI（不返回）──
        if (wcsstr(GetCommandLineW(), L"--record")) {
            RecorderRun();
            return false;
        }

        // ── GUI 单实例：已有实例时等待其退出（管理员重启流程中旧实例会延迟退出），
        //    超时后按旧行为唤起已有窗口并退出 ──
        HANDLE guiMutex = CreateMutexW(nullptr, TRUE, kIpcGuiMutex);
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            const bool elevatedNow = RunningElevated();
            for (int i = 0; i < 40 && elevatedNow; ++i) {   // 提权重启接管：最多等 4 秒
                CloseHandle(guiMutex);
                Sleep(100);
                guiMutex = CreateMutexW(nullptr, TRUE, kIpcGuiMutex);
                if (GetLastError() != ERROR_ALREADY_EXISTS) break;
            }
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                RecorderShowGui();   // 不是重启流程 → 唤起已有窗口
                ExitProcess(0);
            }
        }
        (void)guiMutex;   // 持有至进程退出

        // ── 确保记录进程在跑；拉不起来则 GUI 兜底自己记录（老行为）──
        // 提权实例接管时，先把普通权限的旧记录进程终结并重新拉起（升级为高完整性，
        // 管理员窗口/反作弊游戏内也能记录）。
        if (RunningElevated()) KillOtherInstances();
        g_selfRecording = !EnsureRecorderRunning();
        if (g_selfRecording) InstallHook();
        atexit([] {
            RemoveHook();
            StorageFlushNow();
        });

        StorageInit();
        LoadThemePref();   // 先于 config 求值，clearColor 才能拿到正确的主题底色
        LoadFontPref();    // 字体缩放偏好（自动/自定义）

        // 自启动已开启且本进程已提权、而管理员模式标记存在时：把计划任务升级为最高权限
        // （管理员开关刚开启、或旧任务仍是普通权限的场景）。幂等，重复执行无害。
        if (RunningElevated() && AdminModeFlagged() && AutostartEnabled()) {
            AutostartSet(false);
            AutostartSet(true);
        }

        g_startHidden = wcsstr(GetCommandLineW(), L"--background") != nullptr;

        // 调试用：--page=N 直接打开指定页面（截图/排查时省得点）
        if (const wchar_t* p = wcsstr(GetCommandLineW(), L"--page=")) {
            const int n = _wtoi(p + 7);
            if (n >= 0 && n <= 3) g_page = n;
        }
        // 调试用：--pick=1/2 启动即打开主题色/热力色取色浮层
        if (const wchar_t* p = wcsstr(GetCommandLineW(), L"--pick=")) {
            const int n = _wtoi(p + 7);
            if (n >= 1 && n <= 2) g_debugPick = n;
        }
        return true;
    }();
    (void)coreReady;

    static const DslAppConfig config = DslAppConfig{}
        .title("KeyboardStats 键盘热力统计")
        .pageId("kbstats")
        .clearColor({g_theme.bg.r, g_theme.bg.g, g_theme.bg.b, 1.0f})
        .windowSize(1180, 720)
        .fps(60.0)
        .iconPath("")   // DEBUG-CHINESE-PATH: temporary disable
        // 托盘属于记录进程（常驻的那个）；GUI 不再挂第二个图标
        .tray(false)
        .onKeyEvent([](const eui::KeyEvent& e) {
            if (!e.isDown()) return;
            if (e.key == eui::InputKey::Left || e.key == eui::InputKey::PageUp) {
                g_page = (g_page + 3) % 4; app::requestUpdate();
            } else if (e.key == eui::InputKey::Right || e.key == eui::InputKey::PageDown) {
                g_page = (g_page + 1) % 4; app::requestUpdate();
            }
        });
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    EnsureUiServices();
    // 拖动边框期间保持字号缩放不变（宽度仍实时用于布局）：
    // 否则每个中间宽度都会触发一次全量字形重建，拖动帧率会明显下降。
    static float s_stableWidth = 0.0f;
    if (!s_liveResizing || s_stableWidth <= 0.0f) s_stableWidth = screen.width;
    UpdateUiScale(s_stableWidth);

    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.rect("root.bg")
                .size(screen.width, screen.height)
                .color(g_theme.bg)
                .build();
            DrawHeader(ui, screen.width);
            DrawControls(ui, screen);
            if (g_page == 0) {
                DrawBoard(ui, screen);
                DrawHeatPage(ui, screen);
                DrawKeyList(ui, screen);
            } else if (g_page == 1) {
                DrawHistPage(ui, screen);
            } else if (g_page == 2) {
                DrawSettingsPage(ui, screen);
            } else {
                DrawThemePage(ui, screen);
            }
            DrawCloseDialog(ui, screen);     // 关窗确认：盖在所有页面之上
            DrawOnboardDialog(ui, screen);   // 首启引导：盖在一切之上
        })
        .build();
}

} // namespace app
