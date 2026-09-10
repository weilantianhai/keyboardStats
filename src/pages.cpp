// 页面绘制实现：头部/控制行/热力图页/Top10/直方图页
#include "pages.h"
#include "theme.h"
#include "state.h"
#include "ui_util.h"
#include "fontscale.h"
#include "layout.h"
#include "components/components.h"
#include "timeutil.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

namespace app {

namespace {

// Top 10 侧栏展开状态（仅本文件；窄窗口自动隐藏侧栏）
bool s_top10Open = true;

// ────────────────── UI 小构件 ──────────────────

// 布局辅助：所有固定尺寸乘以窗口缩放因子，字号与留白同步变化
inline float Px(float v) { return v * g_uiScale; }

inline std::string FormatScale(float v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2fx", v);
    return buf;
}

// 控制行 Y（副标题下方）与页面内容起始 Y（控制行下方）
inline float ControlsY() { return Px(58.0f) + Px(28.0f) + 6.0f; }
inline float ContentTop() { return ControlsY() + Px(34.0f) + Px(14.0f); }

void MiniButton(core::dsl::Ui& ui, const std::string& id, float x, float y,
                float w, float h, const std::string& label, bool accent,
                std::function<void()> onClick) {
    core::Color base  = accent ? g_theme.selected : g_theme.panel;
    core::Color hover = accent ? Hex(0x3B82F6) : g_theme.panelHi;
    core::Color press = accent ? Hex(0x1D4ED8) : g_theme.panelActive;
    ui.rect(id + ".bg")
        .x(x).y(y).size(w, h)
        .states(base, hover, press)
        .radius(Px(8.0f))
        .border(1.0f, accent ? core::Color{0, 0, 0, 0} : g_theme.border)
        .onClick(std::move(onClick))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();
    ui.text(id + ".t")
        .x(x).y(y).size(w, h)
        .text(label)
        .fontSize(Px(17.0f))
        .lineHeight(Px(20.0f))
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
            .fontSize(std::min(24.0f, std::max(10.0f, h * 0.32f)))
            .lineHeight(std::min(24.0f, std::max(10.0f, h * 0.32f)))
            .color(count > 0 ? Hex(0xFFFFFF) : g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }
}

} // namespace

void DrawHeader(core::dsl::Ui& ui, float w) {
    ui.text("hd.title")
        .x(Px(28.0f)).y(Px(12.0f)).size(w - Px(200.0f), Px(46.0f))
        .text("KeyboardStats 键盘热力统计")
        .fontSize(Px(34.0f)).lineHeight(Px(40.0f))
        .color(g_theme.text)
        .build();
    std::string sub = "共 " + WithCommas(g_stats.total) + " 次按键 · " + g_rangeText;
    ui.text("hd.sub")
        .x(Px(28.0f)).y(Px(58.0f)).size(w - Px(200.0f), Px(28.0f))
        .text(sub)
        .fontSize(Px(19.0f)).lineHeight(Px(26.0f))
        .color(g_theme.textMut)
        .build();
    const float bh = Px(34.0f), by = Px(22.0f);
    MiniButton(ui, "hd.top10", w - 306.0f, by, 160.0f, bh,
               s_top10Open ? "隐藏 Top 10" : "显示 Top 10", false,
               [] { s_top10Open = !s_top10Open; app::requestUpdate(); });
    MiniButton(ui, "hd.theme", w - 136.0f, by, 108.0f, bh,
               g_lightMode ? "深色模式" : "浅色模式", false, ToggleTheme);
}

void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float y = ControlsY();
    const float h = Px(34.0f);

    // segmented 组件自身无定位方法，用带位置的 stack 容器承载
    // 横向位置/宽度不随缩放变化（受窗口宽度约束），只缩放高度
    ui.stack("ctrl.page")
        .x(28.0f).y(y).size(300.0f, h)
        .content([&] {
            components::segmented(ui, "seg.page")
                .size(300.0f, h)
                .items({"热力图", "直方图", "设置"})
                .selected(g_page)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_page = v; app::requestUpdate(); })
                .build();
        })
        .build();

    ui.stack("ctrl.range")
        .x(340.0f).y(y).size(420.0f, h)
        .content([&] {
            components::segmented(ui, "seg.range")
                .size(420.0f, h)
                .items({"今天", "7 天", "30 天", "全部"})
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
    // 只显示月-日：控件行横向空间有限（日期选择器内仍显示完整日期）
    auto ymdStr = [](uint32_t ymd) {
        if (!ymd) return std::string("选择日期");
        std::string s = Utf8(YmdToStr(ymd));
        return s.size() >= 10 ? s.substr(5) : s;
    };

    MiniButton(ui, "btn.from", 768.0f, y, 120.0f, h, "从 " + ymdStr(g_pendingFrom),
               false, [] { g_fromOpen.set(!g_fromOpen.get()); });
    MiniButton(ui, "btn.to", 896.0f, y, 120.0f, h, "至 " + ymdStr(g_pendingTo),
               false, [] { g_toOpen.set(!g_toOpen.get()); });
    MiniButton(ui, "btn.apply", 1024.0f, y, 76.0f, h, "应用", true, [] {
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
    const float top = ContentTop();
    // Top 10 侧栏开启且窗口足够宽时右侧预留；窄窗口自动隐藏侧栏
    const bool rail = s_top10Open && screen.width >= 1000.0f;
    const float railW = rail ? Px(250.0f) : 0.0f;
    const float availW = screen.width - railW - Px(56.0f);
    const float availH = screen.height - top - Px(70.0f);
    float u = std::min(availW / 24.0f, availH / 6.0f);
    u = std::clamp(u, 24.0f, 80.0f);   // 随窗口缩放，仅保留可用性下限
    const float kx = (screen.width - railW - u * 24.0f) * 0.5f;
    const float ky = top + Px(12.0f);
    const float gap = 2.0f;

    // 键盘面板底
    ui.rect("heat.panel")
        .x(kx - Px(14.0f)).y(ky - Px(14.0f))
        .size(u * 24.0f + Px(28.0f), u * 6.0f + Px(28.0f))
        .color(g_theme.panel)
        .radius(Px(12.0f))
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
    const float ly = ky + u * 6.0f + Px(22.0f);
    const float lx = kx + u * 24.0f - 8.0f * Px(22.0f) - Px(46.0f);
    ui.text("legend.lo")
        .x(lx - Px(34.0f)).y(ly - Px(4.0f)).size(Px(32.0f), Px(20.0f))
        .text("少").fontSize(Px(16.0f)).lineHeight(Px(20.0f))
        .color(g_theme.textMut).horizontalAlign(core::HorizontalAlign::Right)
        .build();
    for (int i = 0; i < 8; ++i) {
        ui.rect("legend.sw." + std::to_string(i))
            .x(lx + i * Px(22.0f)).y(ly)
            .size(Px(20.0f), Px(12.0f))
            .color(HeatColor((i + 0.5) / 8.0))
            .radius(3.0f)
            .build();
    }
    ui.text("legend.hi")
        .x(lx + 8 * Px(22.0f) + Px(4.0f)).y(ly - Px(4.0f)).size(Px(26.0f), Px(20.0f))
        .text("多").fontSize(Px(16.0f)).lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();
}

// Top 10 侧栏：右侧面板，头部带"收起"按钮（开启状态由 s_top10Open 控制）
void DrawTop10(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!(s_top10Open && screen.width >= 1000.0f)) return;

    const float x = screen.width - Px(250.0f);
    const float y = ContentTop();
    const float w = Px(226.0f);
    const float h = screen.height - y - Px(24.0f);

    ui.rect("top.panel")
        .x(x).y(y).size(w, h)
        .color(g_theme.panel)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .build();
    ui.text("top.title")
        .x(x + Px(16.0f)).y(y + Px(14.0f)).size(w - Px(96.0f), Px(26.0f))
        .text("Top 10")
        .fontSize(Px(20.0f)).lineHeight(Px(24.0f))
        .color(g_theme.text)
        .build();
    MiniButton(ui, "top.collapse", x + w - Px(76.0f), y + Px(10.0f), Px(60.0f), Px(30.0f),
               "收起", false, [] { s_top10Open = false; app::requestUpdate(); });

    float rowY = y + Px(50.0f);
    const float rowH = std::min(Px(34.0f), (h - Px(64.0f)) / 10.0f);
    const int n = (int)g_keyHist.size();
    const int start = std::max(0, n - 10);
    int rank = n - start;   // 升序数组的末尾即最大值，倒序输出
    for (int i = n - 1; i >= start; --i) {
        const TopEntry& e = g_keyHist[(size_t)i];
        std::string id = "top.row." + std::to_string(rank);
        ui.text(id + ".name")
            .x(x + Px(16.0f)).y(rowY).size(w * 0.55f, rowH)
            .text(std::to_string(rank) + ". " + e.name)
            .fontSize(Px(17.0f)).lineHeight(rowH)
            .color(g_theme.text)
            .build();
        ui.text(id + ".count")
            .x(x + w * 0.55f).y(rowY).size(w - w * 0.55f - Px(16.0f), rowH)
            .text(WithCommas(e.count))
            .fontSize(Px(17.0f)).lineHeight(rowH)
            .color(g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Right)
            .build();
        ui.rect(id + ".bar.bg")
            .x(x + Px(16.0f)).y(rowY + rowH - Px(4.0f))
            .size(w - Px(32.0f), 2.0f)
            .color(g_theme.idleEdge)
            .radius(1.0f)
            .build();
        ui.rect(id + ".bar")
            .x(x + Px(16.0f)).y(rowY + rowH - Px(4.0f))
            .size((w - Px(32.0f)) * e.frac, 2.0f)
            .color(g_theme.selected)
            .radius(1.0f)
            .transition(Motion())
            .animate(core::AnimProperty::Frame)
            .build();
        rowY += rowH + Px(4.0f);
        --rank;
    }
    if (g_keyHist.empty()) {
        ui.text("top.empty")
            .x(x + Px(16.0f)).y(rowY).size(w - Px(32.0f), Px(24.0f))
            .text("暂无数据，去打几个字吧")
            .fontSize(Px(14.0f)).lineHeight(Px(20.0f))
            .color(g_theme.textMut)
            .build();
    }
}

// 按键使用次数直方图：升序（左低右高），几何/字号/交互与上方 barChart 保持一致
// （同一 plotX/plotY/plotW/plotH、4 条网格线、同圆角比例、hover 态 + 悬浮详情）。
// 键名仅在槽宽放得下时显示（窄窗口不显示），次数信息靠悬浮提示。
void DrawKeyHist(core::dsl::Ui& ui, float x, float y, float w, float h) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    ui.stack("keyhist.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            ui.rect("keyhist.bg")
                .size(w, h)
                .color(tk.surface)
                .radius(m.radius.section)
                .border(1.0f, components::theme::withOpacity(g_theme.border, 0.76f))
                .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
                .build();

            const float titleX = m.spacing.large;
            ui.text("keyhist.title")
                .x(titleX).y(m.typography.control)
                .size(std::max(0.0f, w * 0.55f), m.control.compact)
                .text("按键使用次数分布（左 → 右 升序）")
                .fontSize(m.typography.title)
                .lineHeight(m.typography.title + m.typography.lineGap)
                .color(g_theme.text)
                .build();

            if (g_keyHist.empty()) {
                ui.text("keyhist.empty")
                    .x(titleX).y(m.typography.control + m.typography.title + 12.0f)
                    .size(w - titleX * 2.0f, m.typography.body + 8.0f)
                    .text("暂无数据，去打几个字吧")
                    .fontSize(m.typography.body)
                    .lineHeight(m.typography.body + m.typography.lineGap)
                    .color(g_theme.textMut)
                    .build();
                return;
            }

            // 右上角注解：最多键（键名标签被隐藏时也能看到极值）
            const TopEntry& maxE = g_keyHist.back();
            ui.text("keyhist.max")
                .x(w * 0.5f)
                .y(m.typography.control + (m.typography.title - m.typography.label) * 0.5f)
                .size(std::max(0.0f, w * 0.5f - titleX), m.control.compact)
                .text("最多：" + maxE.name + " " + WithCommas(maxE.count) + " 次")
                .fontSize(m.typography.label)
                .lineHeight(m.typography.label + m.typography.lineGap)
                .color(g_theme.textMut)
                .horizontalAlign(core::HorizontalAlign::Right)
                .build();

            // 绘图区几何与 barChart 相同
            const float plotX = 32.0f;
            const float plotY = 70.0f;
            const float plotW = std::max(1.0f, w - 64.0f);
            const float plotH = std::max(1.0f, h - 112.0f);
            const float bottomY = plotY + plotH;
            const core::Color grid = components::theme::withOpacity(g_theme.border,
                                                                   tk.dark ? 0.38f : 0.36f);
            for (int line = 0; line < 4; ++line) {
                ui.rect("keyhist.grid." + std::to_string(line))
                    .x(plotX).y(plotY + (float)line * plotH / 3.0f)
                    .size(plotW, m.spacing.hairline)
                    .color(grid)
                    .build();
            }

            const int n = (int)g_keyHist.size();
            const float slotW = plotW / (float)n;
            // barChart 用 min(32, max(18, slot*0.54))；键数可达上百，下限放宽到 2px 防溢出
            const float barW = std::min(32.0f, std::max(2.0f, slotW * 0.54f));
            const float labelFont = m.typography.label;
            for (int i = 0; i < n; ++i) {
                const TopEntry& e = g_keyHist[(size_t)i];
                const float frac = std::clamp(e.frac, 0.0f, 1.0f);
                const float barH = std::max(8.0f, frac * plotH);
                const float bx = plotX + (float)i * slotW + (slotW - barW) * 0.5f;
                const float by = bottomY - barH;
                const std::string barId = "keyhist.bar." + std::to_string(i);
                const core::Color color = HeatColor(frac);   // 热力渐变：蓝 → 黄 → 红
                ui.rect(barId)
                    .x(bx).y(by).size(barW, barH)
                    .states(color,
                            core::mixColor(color, core::Color{1, 1, 1, 1}, 0.18f),
                            core::mixColor(color, core::Color{0, 0, 0, 1}, 0.12f))
                    .radius(std::min(m.radius.popup, barW * 0.34f))
                    .instantStates()
                    .transition(Motion())
                    .animate(core::AnimProperty::Frame | core::AnimProperty::Color)
                    .build();

                // 键名：槽宽放得下才显示（窄窗口整体不显示）
                const float estW = (float)e.name.size() * labelFont * 0.62f;
                if (slotW >= estW + 4.0f) {
                    ui.text(barId + ".label")
                        .x(bx - m.spacing.compact).y(h - m.control.menuItem)
                        .size(barW + m.spacing.section, m.control.indicator)
                        .text(e.name)
                        .fontSize(labelFont)
                        .lineHeight(labelFont + m.typography.lineGap)
                        .color(components::theme::withOpacity(g_theme.text, 0.56f))
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .build();
                }

                // 悬浮详情：如 "z 1254 次"
                components::tooltip(ui, barId + ".tooltip")
                    .theme(tk)
                    .source(barId)
                    .value(e.name + " " + WithCommas(e.count) + " 次")
                    .anchor(bx + barW * 0.5f, by)
                    .bounds(w, h)
                    .style(components::TooltipStyle(tk))
                    .build();
            }
        })
        .build();
}

void DrawHistPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float x = Px(28.0f), y = ContentTop();
    const float w = screen.width - Px(56.0f);
    const float totalH = screen.height - y - Px(24.0f);
    // 上：时间直方图（约 52%），下：按键使用次数直方图
    const float topH = totalH * 0.52f;
    const float histY = y + topH + Px(10.0f);
    const float histH = totalH - topH - Px(10.0f);

    // 必须用定位 stack 包裹：barChart 构建器没有定位方法，
    // 直接 build 会从 (0,0) 画起并盖住顶部的页签控制行。
    ui.stack("hist.page")
        .x(x).y(y)
        .size(w, topH)
        .content([&] {
            components::barChart(ui, "hist")
                .size(w, topH)
                .title(g_rangeText)
                .values(g_barVals)
                .labels(g_barLabels)
                .theme(CurrentTheme())
                .transition(Motion())
                .build();
        })
        .build();

    DrawKeyHist(ui, x, histY, w, histH);
}

// 设置页：字体缩放（自动开关 + 无极滑块，统一作用于全部字号）
void DrawSettingsPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;
    const float x = Px(28.0f), y = ContentTop();
    const float w = std::min(screen.width - Px(56.0f), Px(760.0f));
    const float h = std::max(Px(220.0f), std::min(Px(320.0f), screen.height - y - Px(24.0f)));

    ui.rect("set.panel")
        .x(x).y(y).size(w, h)
        .color(tk.surface)
        .radius(m.radius.section)
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
        .build();

    ui.text("set.title")
        .x(x + Px(24.0f)).y(y + Px(18.0f)).size(w - Px(48.0f), Px(30.0f))
        .text("设置")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();

    // ── 行 1：自动开关 ──
    const float row1 = y + Px(74.0f);
    ui.text("set.auto.label")
        .x(x + Px(24.0f)).y(row1).size(w * 0.5f, Px(30.0f))
        .text("字体大小自适应窗口")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    ui.stack("set.auto.row")
        .x(x + w - Px(160.0f)).y(row1 - Px(4.0f)).size(Px(136.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.auto")
                .size(Px(136.0f), Px(38.0f))
                .checked(g_fontAuto)
                .text("自动")
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) { SetFontAuto(v); app::requestUpdate(); })
                .build();
        })
        .build();

    // ── 行 2：字体大小滑块（无极）──
    const float row2 = y + Px(140.0f);
    const float sliderW = w - Px(48.0f) - Px(110.0f);
    const float shown = g_fontAuto ? AutoScaleForWidth(screen.width) : g_fontCustom;
    ui.text("set.slider.label")
        .x(x + Px(24.0f)).y(row2 - Px(26.0f)).size(w * 0.6f, Px(24.0f))
        .text("字体大小")
        .fontSize(m.typography.label)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();
    ui.text("set.slider.value")
        .x(x + w - Px(134.0f)).y(row2 - Px(26.0f)).size(Px(110.0f), Px(24.0f))
        .text(FormatScale(shown))
        .fontSize(m.typography.label)
        .lineHeight(Px(24.0f))
        .color(g_theme.text)
        .horizontalAlign(core::HorizontalAlign::Right)
        .build();

    // 自动模式下不响应拖动，配色变暗以示不可调
    components::SliderStyle sl;
    sl.track = components::theme::withOpacity(g_theme.border, g_fontAuto ? 0.45f : 1.0f);
    sl.fill = g_fontAuto ? components::theme::withOpacity(g_theme.selected, 0.35f)
                         : g_theme.selected;
    sl.knob = g_fontAuto ? g_theme.textMut : g_theme.text;
    ui.stack("set.slider.row")
        .x(x + Px(24.0f)).y(row2 - Px(8.0f)).size(sliderW, Px(46.0f))
        .content([&] {
            components::slider(ui, "set.font")
                .size(sliderW, Px(46.0f))
                .value((shown - kFontScaleMin) / (kFontScaleMax - kFontScaleMin))
                .style(sl)
                .theme(tk)
                .transition(Motion())
                .onChange([](float v) {
                    if (g_fontAuto) return;   // 自动模式：滑块只读
                    const float s = kFontScaleMin + v * (kFontScaleMax - kFontScaleMin);
                    // 组件的同步回调可能带着量化后的值回来：差异过小不入库，避免字号漂移
                    if (std::fabs(s - g_fontCustom) < 0.02f) return;
                    SetFontCustom(s);
                    app::requestUpdate();
                })
                .build();
        })
        .build();

    ui.text("set.slider.min")
        .x(x + Px(24.0f)).y(row2 + Px(44.0f)).size(Px(60.0f), Px(22.0f))
        .text("小").fontSize(m.typography.caption).lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .build();
    ui.text("set.slider.max")
        .x(x + Px(24.0f) + sliderW - Px(60.0f)).y(row2 + Px(44.0f)).size(Px(60.0f), Px(22.0f))
        .text("大").fontSize(m.typography.caption).lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .horizontalAlign(core::HorizontalAlign::Right)
        .build();

    // ── 行 3：说明 ──
    ui.text("set.hint")
        .x(x + Px(24.0f)).y(y + h - Px(66.0f)).size(w - Px(48.0f), Px(48.0f))
        .text("自动：字号随窗口宽度缩放（宽窗口更大、窄窗口更小）。\n"
              "关闭自动后，可拖动滑块统一调整界面全部字体，设置会自动保存。")
        .fontSize(m.typography.caption)
        .lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .build();
}

} // namespace app
