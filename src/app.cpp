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
#include "padpref.h"        // 活跃分数权重 + 摇杆行程折算（列表与看板共用同一口径）
#include "win/adminmode.h"
#include "win/autostart.h"

#include <windows.h>
#include <exception>

#include <algorithm>
#include <cmath>
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
std::vector<float> g_barVals;
std::vector<std::string> g_barLabels;
std::vector<TopEntry> g_keyHist;
std::string g_rangeText;

eui::Signal<bool> g_fromOpen{false};
eui::Signal<bool> g_toOpen{false};
// 按键筛选 g_keyFilter 与各组峰值定义在 heatnorm.cpp（统计层）

static bool g_startHidden = false;
static bool g_uiServicesReady = false;
static UINT_PTR g_timerId = 0;
static bool g_selfRecording = false;   // 记录进程拉不起来时，GUI 兜底自己记录
// 消息窗口句柄 + 记录进程落盘通知的等待线程（数据重载改由事件驱动，退出时清理）
static HWND s_msgWnd = nullptr;
static HANDLE s_dataWaitThread = nullptr;

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

// 调试用（--padseed）：给手柄按键灌入梯度假数据。
// 用途：手边没接手柄时也能验证手柄面板的渲染、热力分组与布局（截图核对用），
// 只在显式加了这个参数时生效，正常启动完全不受影响。
static void MaybeSeedGamepad(RangeStats& s) {
    if (!DebugPadSeed()) return;
    long v = 900;
    for (const PadDef& p : kPadButtons) {
        s.counts[p.vk] += v;
        v = std::max(12L, v * 3 / 4);
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
    MaybeSeedGamepad(s);   // 调试开关 --padseed（见函数定义）

    // 峰值不只是一个数：全局峰值供"全部"模式跨组对比，另外各组各自留一份，
    // 供键盘 / 鼠标 / 手柄 / 分开模式按组独立归一（滚轮不再碾压点击，键盘不再碾压手柄）。
    HeatMaximaFromCounts(s.counts);

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

    // 按键分布：全键参与（含小键盘、鼠标伪键与手柄），未使用的计数为 0，升序排列。
    // 这样小键盘数字（1`、2`…）即使在未使用/NumLock 关闭时也能在直方图中被识别。
    g_keyHist.clear();
    {
        std::vector<TopEntry> all;
        auto add = [&](KeyCode vk) {
            for (const TopEntry& e : all) if (e.vk == vk) return;   // 主键区/小键盘的回车等重复键码
            const wchar_t* nm = StatName(vk);
            TopEntry e;
            e.vk = vk;
            e.name = nm ? Utf8(nm) : ("VK" + std::to_string((int)vk));
            e.count = (vk < kKeySlots) ? s.counts[vk] : 0;
            e.group = KeyGroupOf(vk);
            all.push_back(std::move(e));
        };
        for (int i = 0; i < kKeyCount; ++i) add(kKeys[i].vk);
        for (KeyCode vk : {kMouseLeft, kMouseRight, kMouseMiddle, kMouseX1, kMouseX2,
                           kWheelUp, kWheelDown, kWheelLeft, kWheelRight}) add(vk);
        for (const PadDef& pad : kPadButtons) add(pad.vk);

        // 摇杆/扳机：没有"次数"，只有行程。用与活跃分数**同一套折算**把它们变成
        // 等效次数，才能和按键一起出现在排行榜里（口径不一致会让用户没法验算）：
        //   摇杆 行程→满推往返次数（PadStickTripsFromTravel，走 2 个半径 = 1 次）
        //   扳机 行程本身就是"满按次数"（0..1 = 一次到底）
        // 注意用 PadTravelQuery 而不是 PadTravelToday：列表要跟随当前时间段
        // （今天/7天/30天/自定义），否则切到"最近7天"时手柄那几项永远是今天的数。
        //
        // 这里**必须**再过一道 PadUnitGet()：设置页的"行程显示单位"开关就是靠
        // 这一步起作用的（切到毫米时 count 变成物理毫米数）。早先漏了这道换算，
        // 于是开关只写偏好、界面毫无变化——成了个摆设。
        {
            const PadTravel tv = PadTravelQuery(g_rangeMode, from, to);
            const PadUnit unit = PadUnitGet();
            const auto addAnalog = [&](KeyCode vk, double equivalent, bool isStick) {
                TopEntry e;
                e.vk = vk;
                e.name = Utf8(StatName(vk));
                e.count = (long)std::lround(isStick ? PadStickToDisplay(equivalent, unit)
                                                    : PadTriggerToDisplay(equivalent, unit));
                e.group = KeyGroupOf(vk);
                e.analog = true;
                all.push_back(std::move(e));
            };
            addAnalog(kPadStickL, PadStickTripsFromTravel(tv.stickL), true);
            addAnalog(kPadStickR, PadStickTripsFromTravel(tv.stickR), true);
            addAnalog(kPadTrigL,  tv.trigL, false);
            addAnalog(kPadTrigR,  tv.trigR, false);
        }

        long top1 = 0;
        for (const TopEntry& e : all) top1 = std::max(top1, e.count);
        std::stable_sort(all.begin(), all.end(),
                         [](const TopEntry& a, const TopEntry& b) { return a.count < b.count; });
        for (TopEntry& e : all) {
            e.frac = top1 > 0 ? (float)e.count / (float)top1 : 0.0f;
            g_keyHist.push_back(std::move(e));
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

// 键鼠按下动画 + 手柄模拟量面板的驱动：框架是事件驱动渲染，状态变化不会自己触发重绘。
// 16ms 轮询一次共享状态，只在"最近有输入在动"时请求重绘（平时开销≈0）。
// 必须把 SharedPadAnalogAlive 也算进来：推摇杆/按扳机**不产生键鼠事件**，
// tick 不会变，只看 SharedKeyAlive 的话面板内圈和行程柱永远不刷新。
static void CALLBACK TickKeyAnim(HWND, UINT, UINT_PTR, DWORD) {
    // 调试旁路（--padmirror=<文件>）：注入器进程拿不到共享内存的写入句柄，
    // padTick 不动 → SharedPadAnalogAlive() 恒为 false。此时改读文件，
    // 只要读到合法数值就持续请求重绘，才能验证"数值变了界面跟不跟着动"。
    SharedPadAnalog mirror;
    if (app::DebugPadMirror(&mirror) || SharedKeyAlive() || SharedPadAnalogAlive())
        app::requestUpdate();
}

// 手柄行程（摇杆/扳机）变化的粗比较：只看四个总量。
// 单独做这件事的原因：行程**不进 QueryRange**（那是键鼠的按键表），
// 所以纯推摇杆时 StatsDiffer 恒为 false，看板上的"活跃分数"卡片就不会重算，
// 表现为推摇杆/按扳机分数纹丝不动。
static bool PadTravelDiffer(const PadTravel& a, const PadTravel& b) {
    return a.stickL != b.stickL || a.stickR != b.stickR ||
           a.trigL != b.trigL || a.trigR != b.trigR;
}

// 周期任务：驱动落盘 + 数据节流刷新（单线程）
static void CALLBACK TickTimer(HWND, UINT, UINT_PTR, DWORD) {
    StorageFlushIfDue();

    // 排障（--padiag）：把 GUI 当前对接到的共享内存版本打到 %TEMP%。
    // 这是判断"摇杆为什么不跟着动"的第一个岔路口——低于 3 说明 GUI 被降级到了
    // 旧版本共享内存，模拟量那段字段根本不存在，再怎么查绘制都是白费。
    {
        static bool diag = wcsstr(GetCommandLineW(), L"--padiag") != nullptr;
        if (diag) PadDiagLogVersion();
    }

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
        // 兜底：通知是主路径（记录进程落盘 → PostMessage → 增量重载）。
        // 这里只留一个**低频**自检——防的是通知丢失的边角情况（record 崩溃后被
        // 重新拉起、用户手工往数据文件夹放了文件、事件对象被别的进程抢先销毁等）。
        // 文件没变时 StorageReloadIfChanged 内部只比大小，不读内容，开销≈0；
        // 但仍不该每 500ms 就问一次文件系统，所以拉到 5 秒一次。
        static int tick = 0;
        if (++tick >= 10) {   // 500ms × 10 = 5s
            tick = 0;
            StorageReloadIfChanged();
        }
    }

    TickFontScale(GetTickCount64() / 1000.0);   // 字号滑块：值稳定后才应用

    if (s_enforceLeft > 0) { --s_enforceLeft; EnforceMinSizeOnce(); }

    RangeStats fresh = QueryRange(g_rangeMode, g_customFrom, g_customTo);   // 粗比较即可
    if (StatsDiffer(fresh, g_stats)) {
        FetchStats();
        app::requestUpdate();
    }

    // 行程变化也要重绘（否则纯手柄操作时看板分数不刷新）。
    // 缓存上次的值：只在真的变了才请求重绘，不引入常态重绘开销。
    static PadTravel s_lastTravel;
    static bool s_travelInit = false;
    const PadTravel travel = PadTravelToday();
    if (!s_travelInit || PadTravelDiffer(travel, s_lastTravel)) {
        s_lastTravel = travel;
        s_travelInit = true;
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
    // 结束数据变化等待线程：它正阻塞在事件上，先置退出标志把它唤醒并等它收尾，
    // 免得它在我们销毁窗口后还 PostMessage（g_days 等内存缓存随进程退出自然释放）。
    StorageStopDataChangedWait();
    if (s_dataWaitThread) {
        WaitForSingleObject(s_dataWaitThread, 1000);
        CloseHandle(s_dataWaitThread);
        s_dataWaitThread = nullptr;
    }
    s_msgWnd = nullptr;
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

// 等待线程：阻塞等"记录进程落盘"事件，收到后 PostMessage 唤醒主线程去重载。
// 为什么不直接在定时器里轮询文件：轮询每次都要问文件系统，且数据要等下一个 tick；
// 事件驱动则是"落盘即通知"，延迟从最多 500ms 降到接近 0，空闲时也完全不占 CPU。
static DWORD WINAPI DataChangedWaitProc(LPVOID) {
    while (StorageWaitDataChanged(INFINITE)) {
        if (!s_msgWnd) break;
        if (!PostMessageW(s_msgWnd, WM_APP + 1, 0, 0)) break;
    }
    return 0;
}

static LRESULT CALLBACK MsgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP + 1) {
        // 记录进程刚落盘：增量读入新增的行（内部自己比对偏移，没变就是空跑）
        StorageReloadIfChanged();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void EnsureUiServices() {
    if (g_uiServicesReady) return;
    g_uiServicesReady = true;

    ApplyMinWindowSize();

    // 消息专用窗口：承载落盘/刷新定时器（与 GLFW 消息泵同线程）
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MsgWndProc;
    wc.lpszClassName = L"KeyboardStatsMsgWnd";
    RegisterClassW(&wc);
    HWND msg = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                               nullptr, nullptr, nullptr);
    s_msgWnd = msg;
    if (msg) {
        g_timerId = SetTimer(msg, 1, 500, TickTimer);
        SetTimer(msg, 2, 16, TickKeyAnim);   // 按下动画驱动
    }
    // 数据变化等待线程（GUI 侧）；"GUI 启动时自己加载一次"由 StorageInit 负责
    s_dataWaitThread = CreateThread(nullptr, 0, DataChangedWaitProc, nullptr, 0, nullptr);

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
        //    旁路（--preview-instance）：开发截图需要预览构建与用户正在使用的正式
        //    实例**并存**——否则只要正式版开着，预览进程就会撞上互斥体、唤起别人的
        //    窗口然后自己退出，截图脚本只能拿到"退出码 0"而永远找不到窗口。
        //    仅显式带参数时生效，正常启动行为完全不变。
        if (!wcsstr(GetCommandLineW(), L"--preview-instance")) {
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
        }   // --preview-instance 旁路结束

        // ── 确保记录进程在跑；拉不起来则 GUI 兜底自己记录（老行为）──
        // 提权实例接管时：先请普通权限的旧记录进程**优雅下场**（落盘 + 注销托盘），
        // 再终结漏网的旧实例，最后拉起高完整性记录进程（管理员窗口/反作弊游戏内
        // 也能记录）。直接 Kill 会丢掉它缓冲里最多 5 秒的按键，托盘图标也会突兀消失。
        if (RunningElevated()) {
            RecorderShutdownGracefully(1500);
            KillOtherInstances();
        }
        g_selfRecording = !EnsureRecorderRunning();
        if (g_selfRecording) InstallHook();
        atexit([] {
            RemoveHook();
            StorageFlushNow();
        });

        StorageInit();
        // 提权实例启动时修复存量自启动任务（历史遗留的普通权限任务会开机失败）
        AutostartRepairIfNeeded();
        LoadThemePref();   // 先于 config 求值，clearColor 才能拿到正确的主题底色
        LoadFontPref();    // 字体缩放偏好（自动/自定义）

        g_startHidden = wcsstr(GetCommandLineW(), L"--background") != nullptr;

        // 调试用：--page=N 直接打开指定页面（截图/排查时省得点）
        if (const wchar_t* p = wcsstr(GetCommandLineW(), L"--page=")) {
            const int n = _wtoi(p + 7);
            if (n >= 0 && n <= 3) g_page = n;
        }
        // 调试用：--hladmin 启动即跳转设置页并触发管理员开关闪烁引导
        if (wcsstr(GetCommandLineW(), L"--hladmin")) {
            DebugHighlightAdmin();
        }
        // 自启动的提权收尾：管理员开关/自启动开关触发的提权重启，新实例在此完成
        // 最高权限任务的创建，并把结果写到设置目录（供用户/旧实例确认）
        if (wcsstr(GetCommandLineW(), L"--autostart-elevate")) {
            const AutostartResult r = AutostartEnable();
            const std::wstring out = app::PrefDirPath() + L"\\autostart-result.txt";
            if (FILE* f = _wfopen(out.c_str(), L"wb")) {
                fprintf(f, "AutostartEnable = %s\n",
                        r == AutostartResult::Ok ? "Ok"
                        : (r == AutostartResult::NeedElevation ? "NeedElevation" : "Failed"));
                fclose(f);
            }
        }
        // 调试用：--autostarton 等价于开关开启（含提权需求判断），结果写文件
        if (wcsstr(GetCommandLineW(), L"--autostarton")) {
            const AutostartResult r = AutostartEnable();
            const std::wstring out = app::PrefDirPath() + L"\\autostart-result.txt";
            if (FILE* f = _wfopen(out.c_str(), L"wb")) {
                fprintf(f, "AutostartEnable = %s\n",
                        r == AutostartResult::Ok ? "Ok"
                        : (r == AutostartResult::NeedElevation ? "NeedElevation" : "Failed"));
                fclose(f);
            }
        }
        // 调试用：--pick=1/2 启动即打开主题色/热力色取色浮层
        if (const wchar_t* p = wcsstr(GetCommandLineW(), L"--pick=")) {
            const int n = _wtoi(p + 7);
            if (n >= 1 && n <= 2) g_debugPick = n;
        }
        // 调试用：--filter=N 启动即选中按键筛选（0键盘 1鼠标 2手柄 3全部 4分开）。
        // 用于按筛选模式截图核对热力着色（哪种模式哪个区域该着色）。
        if (const wchar_t* p = wcsstr(GetCommandLineW(), L"--filter=")) {
            const int n = _wtoi(p + 9);
            if (n >= 0 && n <= 4) g_keyFilter = n;
        }
        return true;
    }();
    (void)coreReady;

    static const DslAppConfig config = DslAppConfig{}
        .title("KeyboardStats 键盘热力统计")
        .pageId("kbstats")
        .clearColor({g_theme.bg.r, g_theme.bg.g, g_theme.bg.b, 1.0f})
        .windowSize(1180, 720)
        .fps(90.0)   // 渲染帧率上限：与手柄**采样率**无关（那个在设置页，30/62/125Hz）
        .iconPath(AssetAbs("icon.png"))
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
