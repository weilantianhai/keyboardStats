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
std::vector<TopEntry> g_top;
std::string g_rangeText;

eui::Signal<bool> g_fromOpen{false};
eui::Signal<bool> g_toOpen{false};

static bool g_startHidden = false;
static bool g_uiServicesReady = false;
static UINT_PTR g_timerId = 0;

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

    g_barVals.clear(); g_barLabels.clear();
    for (const Bucket& b : s.buckets) {
        g_barVals.push_back((float)b.count);
        g_barLabels.push_back(Utf8(b.label));
    }

    g_top.clear();
    std::vector<std::pair<long, int>> ranked;   // (count, vk)
    for (int vk = 0; vk < 256; ++vk)
        if (s.counts[vk] > 0) ranked.push_back({s.counts[vk], vk});
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    long top1 = ranked.empty() ? 0 : ranked.front().first;
    for (size_t i = 0; i < ranked.size() && i < 10; ++i) {
        g_top.push_back({Utf8(StatName((uint8_t)ranked[i].second)),
                         ranked[i].first,
                         top1 > 0 ? (float)ranked[i].first / (float)top1 : 0.0f});
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

    RangeStats fresh = QueryRange(g_rangeMode, g_customFrom, g_customTo);   // 粗比较即可
    if (StatsDiffer(fresh, g_stats)) {
        FetchStats();
        app::requestUpdate();
    }
}

static void EnsureUiServices() {
    if (g_uiServicesReady) return;
    g_uiServicesReady = true;

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
                g_page = 0; app::requestUpdate();
            } else if (e.key == eui::InputKey::Right || e.key == eui::InputKey::PageDown) {
                g_page = 1; app::requestUpdate();
            }
        });
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    EnsureUiServices();

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
                if (screen.width >= 980.0f) {
                    DrawTop10(ui, screen);
                }
            } else {
                DrawHistPage(ui, screen);
            }
        })
        .build();
}

} // namespace app
