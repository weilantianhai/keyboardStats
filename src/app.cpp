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

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace app {

// ────────────────── 运行状态定义（声明见 state.h） ──────────────────

int  g_page = 0;
int  g_rangeMode = 3;
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

// 周期任务：驱动落盘 + 数据节流刷新（单线程）
static void CALLBACK TickTimer(HWND, UINT, UINT_PTR, DWORD) {
    StorageFlushIfDue();

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

static LRESULT CALLBACK MinSizeWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
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
    if (msg) g_timerId = SetTimer(msg, 1, 500, TickTimer);

    FetchStats();

    if (g_startHidden) {
        // 自启动静默启动：框架无"初始隐藏"，退化为启动即最小化
        HWND main = GetActiveWindow();
        if (main) ShowWindow(main, SW_MINIMIZE);
    }
}

static std::string ExeDirA() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p = buf;
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

static std::string AssetAbs(const char* name) { return ExeDirA() + "assets\\" + name; }

// ────────────────── EUI-NEO 应用入口（框架提供 main） ──────────────────

const DslAppConfig& dslAppConfig() {
    // 单实例 + 存储 + 钩子 + 主题：必须在窗口出现前完成（静态初始化器时机最早）
    static const bool coreReady = [] {
        HANDLE mutex = CreateMutexW(nullptr, TRUE, L"KeyboardStats.SingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxW(nullptr, L"KeyboardStats 已在运行（请查看系统托盘）",
                        L"KeyboardStats", MB_OK | MB_ICONINFORMATION);
            ExitProcess(0);
        }
        (void)mutex;   // 持有至进程退出

        StorageInit();
        InstallHook();
        atexit([] {
            RemoveHook();
            StorageFlushNow();
        });

        LoadThemePref();   // 先于 config 求值，clearColor 才能拿到正确的主题底色
        LoadFontPref();    // 字体缩放偏好（自动/自定义）

        g_startHidden = wcsstr(GetCommandLineW(), L"--background") != nullptr;
        return true;
    }();
    (void)coreReady;

    static const DslAppConfig config = DslAppConfig{}
        .title("KeyboardStats 键盘热力统计")
        .pageId("kbstats")
        .clearColor({g_theme.bg.r, g_theme.bg.g, g_theme.bg.b, 1.0f})
        .windowSize(1180, 720)
        .fps(60.0)
        .iconPath(AssetAbs("icon.png"))
        .tray(true)
        .trayTitle("KeyboardStats")
        .trayIcon(AssetAbs("icon.png"))
        .onKeyEvent([](const eui::KeyEvent& e) {
            if (!e.isDown()) return;
            if (e.key == eui::InputKey::Left || e.key == eui::InputKey::PageUp) {
                g_page = (g_page + 2) % 3; app::requestUpdate();
            } else if (e.key == eui::InputKey::Right || e.key == eui::InputKey::PageDown) {
                g_page = (g_page + 1) % 3; app::requestUpdate();
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
                DrawHeatPage(ui, screen);
                DrawKeyList(ui, screen);
            } else if (g_page == 1) {
                DrawHistPage(ui, screen);
            } else {
                DrawSettingsPage(ui, screen);
            }
        })
        .build();
}

} // namespace app
