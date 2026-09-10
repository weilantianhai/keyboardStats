// 页面绘制实现：头部/控制行/热力图页/Top10/直方图页
#include "pages.h"
#include "theme.h"
#include "state.h"
#include "ui_util.h"
#include "layout.h"
#include "components/components.h"
#include "timeutil.h"

#include <algorithm>
#include <functional>
#include <string>

namespace app {

namespace {

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

} // namespace

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

void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float top = 148.0f;
    // 下方预留：图例 + 按键次数直方图面板 + 底边距
    const float availW = screen.width - 56.0f;
    const float availH = screen.height - top - 210.0f;
    float u = std::min(availW / 24.0f, availH / 6.0f);
    u = std::clamp(u, 24.0f, 80.0f);   // 随窗口缩放，仅保留可用性下限
    const float kx = (screen.width - u * 24.0f) * 0.5f;
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

// 按键使用次数直方图：键盘正下方，按次数升序（左低右高），色条沿用热力渐变
void DrawKeyHist(core::dsl::Ui& ui, const eui::Screen& screen) {
    // 与 DrawHeatPage 相同的布局参数，保证面板与键盘对齐
    const float top = 148.0f;
    const float availW = screen.width - 56.0f;
    const float availH = screen.height - top - 210.0f;
    float u = std::min(availW / 24.0f, availH / 6.0f);
    u = std::clamp(u, 24.0f, 80.0f);
    const float kx = (screen.width - u * 24.0f) * 0.5f;
    const float ky = top + 12.0f;

    const float px = kx - 14.0f;
    const float pw = u * 24.0f + 28.0f;
    const float py = ky + u * 6.0f + 48.0f;            // 键盘面板底 + 图例行
    const float ph = std::max(90.0f, std::min(150.0f, screen.height - py - 12.0f));

    ui.rect("keyhist.panel")
        .x(px).y(py).size(pw, ph)
        .color(g_theme.panel)
        .radius(12.0f)
        .border(1.0f, g_theme.border)
        .build();
    ui.text("keyhist.title")
        .x(px + 16.0f).y(py + 10.0f).size(pw - 32.0f, 20.0f)
        .text("按键使用次数分布（左 → 右 升序）")
        .fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.textMut)
        .build();

    if (g_keyHist.empty()) {
        ui.text("keyhist.empty")
            .x(px + 16.0f).y(py + 40.0f).size(pw - 32.0f, 24.0f)
            .text("暂无数据，去打几个字吧")
            .fontSize(14.0f).lineHeight(20.0f)
            .color(g_theme.textMut)
            .build();
        return;
    }

    const float pad = 14.0f;
    const float barsTop = py + 38.0f;
    const float barsBottom = py + ph - 12.0f;
    const float barsH = std::max(20.0f, barsBottom - barsTop);
    const float innerW = pw - pad * 2.0f;
    const int n = (int)g_keyHist.size();
    float barW = innerW / (float)n;
    barW = std::clamp(barW, 3.0f, 20.0f);
    const float groupW = barW * (float)n;
    const float startX = px + pad + std::max(0.0f, (innerW - groupW) * 0.5f);

    for (int i = 0; i < n; ++i) {
        const TopEntry& e = g_keyHist[(size_t)i];
        const float frac = std::clamp(e.frac, 0.0f, 1.0f);
        const float bh = std::max(2.0f, barsH * frac);
        const std::string id = "keyhist.bar." + std::to_string(i);
        ui.rect(id)
            .x(startX + (float)i * barW).y(barsBottom - bh)
            .size(std::max(2.0f, barW - 2.0f), bh)
            .color(HeatColor(frac))   // 与键帽热力同一语义：蓝 → 黄 → 红
            .radius(2.0f)
            .transition(Motion())
            .animate(core::AnimProperty::Frame)
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

} // namespace app
