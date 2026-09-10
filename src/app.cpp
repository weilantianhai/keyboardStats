// KeyboardStats — EUI-NEO 现代化 UI
// 设计令牌源自 ui-ux-pro-max-skill 生成结果（Data-Dense Dashboard，深色）
// 数据层（hook/storage/timeutil/layout）零改动复用
#include "eui_neo.h"
#include "components/components.h"

#include "hook.h"
#include "storage.h"
#include "timeutil.h"
#include "layout.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace app {
namespace {

// ────────────────── 设计令牌（uiux-pro-max: Data-Dense Dashboard） ──────────────────

core::Color Hex(unsigned rgb, float a = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f,
            ((rgb >> 8) & 0xFF) / 255.0f,
            (rgb & 0xFF) / 255.0f, a};
}

// ────────────────── 主题令牌（深 / 浅两套，运行时可切换） ──────────────────

struct UiTheme {
    core::Color bg;         // 页面背景
    core::Color panel;      // 卡片/面板
    core::Color panelHi;    // 卡片悬停
    core::Color panelActive;// 卡片按下
    core::Color text;       // 主文本
    core::Color textMut;    // 次文本
    core::Color border;
    core::Color brand;      // uiux Primary
    core::Color selected;   // 控件选中色
    core::Color accent;     // uiux Accent
    core::Color idleKey;    // 无按键键帽
    core::Color idleEdge;   // 无按键键帽描边
    core::Color heatLo;     // 热度下限（蓝）
    core::Color heatMid;    // 热度中点（黄）
    core::Color heatHi;     // 热度上限（红）
};

UiTheme DarkThemeTokens() {
    UiTheme t;
    t.bg         = Hex(0x0B0C10);
    t.panel      = Hex(0x141822);
    t.panelHi    = Hex(0x1B2130);
    t.panelActive= Hex(0x232B3E);
    t.text       = Hex(0xE5E9F0);   // 对比度 ~15:1
    t.textMut    = Hex(0x8A93A6);
    t.border     = Hex(0x272D3B);
    t.brand      = Hex(0x1E40AF);
    t.selected   = Hex(0x2563EB);   // 白字对比 4.5:1
    t.accent     = Hex(0xD97706);
    t.idleKey    = Hex(0x232A3A);
    t.idleEdge   = Hex(0x323A4E);
    t.heatLo     = Hex(0x313695);
    t.heatMid    = Hex(0xF0DC78);
    t.heatHi     = Hex(0xB2182B);
    return t;
}

UiTheme LightThemeTokens() {
    UiTheme t;
    t.bg         = Hex(0xF2F4F8);
    t.panel      = Hex(0xFFFFFF);
    t.panelHi    = Hex(0xEEF2F8);
    t.panelActive= Hex(0xE3EAF4);
    t.text       = Hex(0x1F2430);   // 对比度 ~14:1
    t.textMut    = Hex(0x5B6472);
    t.border     = Hex(0xD8DEE9);
    t.brand      = Hex(0x1E40AF);
    t.selected   = Hex(0x2563EB);   // 白字对比 4.5:1
    t.accent     = Hex(0xB45309);   // 深琥珀：浅底可读性更好
    t.idleKey    = Hex(0xE6EAF2);
    t.idleEdge   = Hex(0xC9D2E0);
    t.heatLo     = Hex(0x313695);   // 热度梯度两套主题保持一致
    t.heatMid    = Hex(0xF0DC78);
    t.heatHi     = Hex(0xB2182B);
    return t;
}

UiTheme g_theme = DarkThemeTokens();
bool g_lightMode = false;

core::Color HeatColor(double t) {
    if (t <= 0.0) return g_theme.idleKey;
    struct Stop { double t; core::Color c; };
    const Stop kStops[] = {{0.0, g_theme.heatLo}, {0.5, g_theme.heatMid}, {1.0, g_theme.heatHi}};
    for (int i = 0; i < 2; ++i) {
        if (t <= kStops[i + 1].t) {
            double k = (t - kStops[i].t) / (kStops[i + 1].t - kStops[i].t);
            auto lerp = [k](float a, float b) { return float(a + (b - a) * k); };
            return {lerp(kStops[i].c.r, kStops[i + 1].c.r),
                    lerp(kStops[i].c.g, kStops[i + 1].c.g),
                    lerp(kStops[i].c.b, kStops[i + 1].c.b), 1.0f};
        }
    }
    return g_theme.heatHi;
}

components::theme::ThemeColorTokens AppTheme() {
    auto t = g_lightMode ? components::theme::light() : components::theme::dark();
    t.background    = g_theme.bg;
    t.surface       = g_theme.panel;
    t.surfaceHover  = g_theme.panelHi;
    t.surfaceActive = g_theme.panelActive;
    t.text          = g_theme.text;
    t.border        = g_theme.border;
    t.primary       = g_theme.selected;
    // 整体字号调大：分段控件/柱状图/日期选择器等组件统一放大
    t.metrics.typography.caption  += 2.0f;
    t.metrics.typography.label    += 2.0f;
    t.metrics.typography.body     += 2.0f;
    t.metrics.typography.title    += 2.0f;
    t.metrics.typography.display  += 2.0f;
    t.metrics.typography.subtitle += 2.0f;
    t.metrics.typography.input    += 2.0f;
    return t;
}
components::theme::ThemeColorTokens CurrentTheme() { return AppTheme(); }

// ────────────────── 主题偏好持久化 + 系统默认 ──────────────────

std::wstring ThemePrefPath() {
    wchar_t custom[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"KEYBOARDSTATS_DIR", custom, MAX_PATH) > 0) {
        return std::wstring(custom) + L"\\ui-theme.txt";
    }
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    return std::wstring(appdata) + L"\\KeyboardStats\\ui-theme.txt";
}

void SaveThemePref(bool light) {
    FILE* f = _wfopen(ThemePrefPath().c_str(), L"wb");
    if (f) { fputs(light ? "light" : "dark", f); fclose(f); }
}

void LoadThemePref() {
    FILE* f = _wfopen(ThemePrefPath().c_str(), L"rb");
    if (f) {
        char buf[16] = {};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = 0;
        g_lightMode = strstr(buf, "light") != nullptr;
    } else {
        // 无保存偏好：跟随系统（AppsUseLightTheme：1=浅色）
        DWORD v = 0, size = sizeof(v);
        if (RegGetValueW(HKEY_CURRENT_USER,
                         L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS) {
            g_lightMode = v == 1;
        }
    }
    g_theme = g_lightMode ? LightThemeTokens() : DarkThemeTokens();
}

void ToggleTheme() {
    g_lightMode = !g_lightMode;
    g_theme = g_lightMode ? LightThemeTokens() : DarkThemeTokens();
    SaveThemePref(g_lightMode);
    app::requestUpdate();
}
core::Transition Motion() { return core::Transition::make(0.18f, core::Ease::OutCubic); }

// ────────────────── 运行状态（GLFW 主线程单线程访问） ──────────────────

struct TopEntry { std::string name; long count = 0; float frac = 0.0f; };

int  g_page = 0;              // 0=热力图 1=直方图
int  g_rangeMode = 3;         // 0=今天 1=7天 2=30天 3=全部 4=自定义
uint32_t g_customFrom = 0, g_customTo = 0;      // 已应用的 yyyymmdd
uint32_t g_pendingFrom = 0, g_pendingTo = 0;    // 日期选择器中未应用的值

RangeStats g_stats;
long g_maxKey = 0;
std::vector<float> g_barVals;
std::vector<std::string> g_barLabels;
std::vector<TopEntry> g_top;
std::string g_rangeText;

eui::Signal<bool> g_fromOpen{false};
eui::Signal<bool> g_toOpen{false};

bool g_startHidden = false;
bool g_uiServicesReady = false;
UINT_PTR g_timerId = 0;

// ────────────────── 工具 ──────────────────

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string WithCommas(long v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", v);
    std::string s = buf;
    for (int pos = (int)s.size() - 3; pos > 0; pos -= 3) s.insert(pos, ",");
    return s;
}

std::string ExeDirA() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p = buf;
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

std::string AssetAbs(const char* name) { return ExeDirA() + "assets\\" + name; }

uint32_t YmdOf(int y, int m, int d) {
    return (uint32_t)(y * 10000 + m * 100 + d);
}

// ────────────────── 数据获取与节流刷新 ──────────────────

std::string RangeTitle() {
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

bool StatsDiffer(const RangeStats& a, const RangeStats& b) {
    if (a.total != b.total) return true;
    if (memcmp(a.counts, b.counts, sizeof(a.counts)) != 0) return true;
    if (a.buckets.size() != b.buckets.size()) return true;
    for (size_t i = 0; i < a.buckets.size(); ++i)
        if (a.buckets[i].count != b.buckets[i].count) return true;
    return false;
}

// 周期任务：驱动落盘 + 数据节流刷新（2Hz，单线程）
void CALLBACK TickTimer(HWND, UINT, UINT_PTR, DWORD) {
    StorageFlushIfDue();

    RangeStats fresh = QueryRange(g_rangeMode, g_customFrom, g_customTo);   // 粗比较即可
    if (StatsDiffer(fresh, g_stats)) {
        FetchStats();
        app::requestUpdate();
    }
}

void EnsureUiServices() {
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

// ────────────────── UI 小构件 ──────────────────

void MiniButton(core::dsl::Ui& ui, const std::string& id, float x, float y,
                float w, float h, const std::string& label, bool accent,
                std::function<void()> onClick) {
    core::Color base  = accent ? g_theme.selected : g_theme.panel;
    core::Color hover = accent ? Hex(0x3B82F6) : g_theme.panelHi;
    core::Color press = accent ? Hex(0x1D4ED8) : g_theme.panelActive;
    ui.rect(id + ".bg")
        .x(x).y(y).size(w, h)
        .states(base, hover, press)
        .radius(8.0f)
        .border(1.0f, accent ? core::Color{0, 0, 0, 0} : g_theme.border)
        .onClick(std::move(onClick))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();
    ui.text(id + ".t")
        .x(x).y(y).size(w, h)
        .text(label)
        .fontSize(15.0f)
        .lineHeight(16.0f)
        .color(accent ? Hex(0xFFFFFF) : g_theme.text)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

void DrawHeader(core::dsl::Ui& ui, float w) {
    ui.text("hd.title")
        .x(28.0f).y(14.0f).size(w - 200.0f, 40.0f)
        .text("KeyboardStats 键盘热力统计")
        .fontSize(30.0f).lineHeight(36.0f)
        .color(g_theme.text)
        .build();
    std::string sub = "共 " + WithCommas(g_stats.total) + " 次按键 · " + g_rangeText;
    ui.text("hd.sub")
        .x(28.0f).y(58.0f).size(w - 200.0f, 24.0f)
        .text(sub)
        .fontSize(16.0f).lineHeight(22.0f)
        .color(g_theme.textMut)
        .build();
    MiniButton(ui, "hd.theme", w - 122.0f, 22.0f, 94.0f, 30.0f,
               g_lightMode ? "深色模式" : "浅色模式", false, ToggleTheme);
}

void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float y = 96.0f, h = 34.0f;

    // segmented 组件自身无定位方法，用带位置的 stack 容器承载
    ui.stack("ctrl.page")
        .x(28.0f).y(y).size(200.0f, h)
        .content([&] {
            components::segmented(ui, "seg.page")
                .size(200.0f, h)
                .items({"热力图", "直方图"})
                .selected(g_page)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_page = v; app::requestUpdate(); })
                .build();
        })
        .build();

    ui.stack("ctrl.range")
        .x(248.0f).y(y).size(380.0f, h)
        .content([&] {
            components::segmented(ui, "seg.range")
                .size(380.0f, h)
                .items({"今天", "最近 7 天", "最近 30 天", "全部"})
                .selected(g_rangeMode <= 3 ? g_rangeMode : 3)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_rangeMode = v; FetchStats(); app::requestUpdate(); })
                .build();
        })
        .build();

    uint32_t today = TodayLocal();
    if (!g_pendingFrom) g_pendingFrom = AddDays(today, -6);
    if (!g_pendingTo) g_pendingTo = today;
    auto ymdStr = [](uint32_t ymd) {
        if (!ymd) return std::string("选择日期");
        return Utf8(YmdToStr(ymd));
    };

    MiniButton(ui, "btn.from", 620.0f, y, 96.0f, h, "从 " + ymdStr(g_pendingFrom),
               false, [] { g_fromOpen.set(!g_fromOpen.get()); });
    MiniButton(ui, "btn.to", 724.0f, y, 96.0f, h, "至 " + ymdStr(g_pendingTo),
               false, [] { g_toOpen.set(!g_toOpen.get()); });
    MiniButton(ui, "btn.apply", 832.0f, y, 72.0f, h, "应用", true, [] {
        if (g_pendingFrom && g_pendingTo) {
            g_customFrom = g_pendingFrom;
            g_customTo = g_pendingTo;
            g_rangeMode = 4;
            FetchStats();
            app::requestUpdate();
        }
    });

    components::datePicker(ui, "dp.from")
        .screen(screen.width, screen.height)
        .date((int)(g_pendingFrom / 10000), (int)(g_pendingFrom / 100 % 100), (int)(g_pendingFrom % 100))
        .bindOpen(g_fromOpen)
        .theme(CurrentTheme())
        .zIndex(600)
        .onChange([](int y, int m, int d) { g_pendingFrom = YmdOf(y, m, d); app::requestUpdate(); })
        .build();
    components::datePicker(ui, "dp.to")
        .screen(screen.width, screen.height)
        .date((int)(g_pendingTo / 10000), (int)(g_pendingTo / 100 % 100), (int)(g_pendingTo % 100))
        .bindOpen(g_toOpen)
        .theme(CurrentTheme())
        .zIndex(600)
        .onChange([](int y, int m, int d) { g_pendingTo = YmdOf(y, m, d); app::requestUpdate(); })
        .build();
}

void DrawKeycap(core::dsl::Ui& ui, int idx, float x, float y, float w, float h,
                long count, double t) {
    const std::string id = "key." + std::to_string(idx);
    core::Color fill = HeatColor(t);
    core::Color edge = count > 0 ? core::Color{0, 0, 0, 0} : g_theme.idleEdge;
    ui.rect(id)
        .x(x).y(y).size(w, h)
        .color(fill)
        .radius(std::min(8.0f, h * 0.28f))
        .border(1.0f, edge)
        .states(fill,
                count > 0 ? core::mixColor(fill, Hex(0xFFFFFF), 0.10f) : g_theme.panelHi,
                count > 0 ? core::mixColor(fill, Hex(0xFFFFFF), 0.18f) : Hex(0x2A3245))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();

    // 键帽文字：仅宽度足够的键（Space/Shift/Enter 等宽键 + 单键宽度 ≥34px）
    const wchar_t* cap = kKeys[idx].cap;
    if (kKeys[idx].w * 1.0f >= 2.0f || w >= 34.0f) {
        ui.text(id + ".t")
            .x(x).y(y).size(w, h)
            .text(Utf8(cap))
            .fontSize(std::min(20.0f, std::max(9.0f, h * 0.32f)))
            .lineHeight(std::min(20.0f, std::max(9.0f, h * 0.32f)))
            .color(count > 0 ? Hex(0xFFFFFF) : g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }
    // 计数：只标注 Space（唯一的 6.25u 超宽键），其余次数看 Top 10
    if (kKeys[idx].vk == 0x20 && kKeys[idx].w > 4.0f && count > 0) {
        ui.text(id + ".c")
            .x(x).y(y + h * 0.52f).size(w, h * 0.4f)
            .text(WithCommas(count))
            .fontSize(std::min(18.0f, std::max(12.0f, h * 0.26f)))
            .lineHeight(std::min(19.0f, std::max(13.0f, h * 0.28f)))
            .color(Hex(0x1E293B))
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Top)
            .build();
    }
}

void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float top = 148.0f;
    // Top10 右栏在宽窗口下固定占 250px，键盘只使用剩余区域；窄窗口隐藏右栏
    const bool showTop10 = screen.width >= 980.0f;
    const float railW = showTop10 ? 250.0f : 0.0f;
    const float availW = screen.width - railW - 56.0f;
    const float availH = screen.height - top - 70.0f;
    float u = std::min(availW / 24.0f, availH / 6.0f);
    u = std::clamp(u, 24.0f, 80.0f);   // 随窗口缩放，仅保留可用性下限
    const float kx = (screen.width - railW - u * 24.0f) * 0.5f;
    const float ky = top + 12.0f;
    const float gap = 2.0f;

    // 键盘面板底
    ui.rect("heat.panel")
        .x(kx - 14.0f).y(ky - 14.0f)
        .size(u * 24.0f + 28.0f, u * 6.0f + 28.0f)
        .color(g_theme.panel)
        .radius(12.0f)
        .border(1.0f, g_theme.border)
        .build();

    const int n = (int)(sizeof(kKeys) / sizeof(kKeys[0]));
    for (int i = 0; i < n; ++i) {
        const KeyDef& k = kKeys[i];
        long c = (k.vk < 256) ? g_stats.counts[k.vk] : 0;
        double t = g_maxKey > 1 ? (double)c / (double)g_maxKey : 0.0;
        if (c > 0 && t < 0.08) t = 0.08;   // 有按键但占比极小：给最低可见热度
        DrawKeycap(ui, i, kx + k.x * u + gap, ky + k.y * u + gap,
                   k.w * u - gap * 2.0f, k.h * u - gap * 2.0f, c, t);
    }

    // 图例：8 段渐变
    const float ly = ky + u * 6.0f + 22.0f;
    const float lx = kx + u * 24.0f - 8.0f * 22.0f - 46.0f;
    ui.text("legend.lo")
        .x(lx - 30.0f).y(ly - 3.0f).size(28.0f, 18.0f)
        .text("少").fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.textMut).horizontalAlign(core::HorizontalAlign::Right)
        .build();
    for (int i = 0; i < 8; ++i) {
        ui.rect("legend.sw." + std::to_string(i))
            .x(lx + i * 22.0f).y(ly)
            .size(20.0f, 12.0f)
            .color(HeatColor((i + 0.5) / 8.0))
            .radius(3.0f)
            .build();
    }
    ui.text("legend.hi")
        .x(lx + 8 * 22.0f + 4.0f).y(ly - 3.0f).size(24.0f, 18.0f)
        .text("多").fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.textMut)
        .build();
}

void DrawTop10(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float x = screen.width - 250.0f;
    const float y = 148.0f;
    const float w = 226.0f;
    const float h = screen.height - y - 24.0f;

    ui.rect("top.panel")
        .x(x).y(y).size(w, h)
        .color(g_theme.panel)
        .radius(12.0f)
        .border(1.0f, g_theme.border)
        .build();
    ui.text("top.title")
        .x(x + 16.0f).y(y + 12.0f).size(w - 32.0f, 24.0f)
        .text("Top 10")
        .fontSize(17.0f).lineHeight(22.0f)
        .color(g_theme.text)
        .build();

    float rowY = y + 46.0f;
    const float rowH = std::min(30.0f, (h - 60.0f) / 10.0f);
    int rank = 1;
    for (const TopEntry& e : g_top) {
        std::string id = "top.row." + std::to_string(rank);
        ui.text(id + ".name")
            .x(x + 16.0f).y(rowY).size(w * 0.55f, rowH)
            .text(std::to_string(rank) + ". " + e.name)
            .fontSize(14.0f).lineHeight(rowH)
            .color(g_theme.text)
            .build();
        ui.text(id + ".count")
            .x(x + w * 0.55f).y(rowY).size(w - w * 0.55f - 16.0f, rowH)
            .text(WithCommas(e.count))
            .fontSize(14.0f).lineHeight(rowH)
            .color(g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Right)
            .build();
        ui.rect(id + ".bar.bg")
            .x(x + 16.0f).y(rowY + rowH - 4.0f)
            .size(w - 32.0f, 2.0f)
            .color(Hex(0x232A3A))
            .radius(1.0f)
            .build();
        ui.rect(id + ".bar")
            .x(x + 16.0f).y(rowY + rowH - 4.0f)
            .size((w - 32.0f) * e.frac, 2.0f)
            .color(g_theme.selected)
            .radius(1.0f)
            .transition(Motion())
            .animate(core::AnimProperty::Frame)
            .build();
        rowY += rowH + 4.0f;
        ++rank;
    }
    if (g_top.empty()) {
        ui.text("top.empty")
            .x(x + 16.0f).y(rowY).size(w - 32.0f, 24.0f)
            .text("暂无数据，去打几个字吧")
            .fontSize(14.0f).lineHeight(20.0f)
            .color(g_theme.textMut)
            .build();
    }
}

void DrawHistPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float x = 28.0f, y = 148.0f;
    const float w = screen.width - 56.0f;
    const float h = screen.height - y - 24.0f;

    // 必须用定位 stack 包裹：barChart 构建器没有定位方法，
    // 直接 build 会从 (0,0) 画起并盖住顶部的页签控制行。
    ui.stack("hist.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            components::barChart(ui, "hist")
                .size(w, h)
                .title(g_rangeText)
                .values(g_barVals)
                .labels(g_barLabels)
                .theme(CurrentTheme())
                .transition(Motion())
                .build();
        })
        .build();
}

} // namespace

// ────────────────── EUI-NEO 应用入口（框架提供 main） ──────────────────

const DslAppConfig& dslAppConfig() {
    // 单实例 + 存储 + 钩子：必须在窗口出现前完成（静态初始化器时机最早）
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




