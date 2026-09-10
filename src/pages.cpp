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

// ────────────────── 面板尺寸无极调节（右下角指示器拖动，持久化） ──────────────────

float s_kbScale = 1.0f;      // 键盘盒尺寸系数
float s_mouseScale = 1.0f;   // 鼠标盒尺寸系数
float s_listScale = 1.0f;    // 右侧按键列表宽度系数
bool  s_layoutLoaded = false;

void EnsureLayoutPrefs() {
    if (s_layoutLoaded) return;
    s_layoutLoaded = true;
    s_kbScale    = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "kb", "1.0").c_str()), 0.35f, 2.0f);
    s_mouseScale = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "mouse", "1.0").c_str()), 0.4f, 3.0f);
    s_listScale  = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "list", "1.0").c_str()), 0.6f, 2.0f);
}

void SaveLayoutPref(const char* key, float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", value);
    PrefSetValue(L"ui-layout.txt", key, buf);
}

// 面板右下角外侧的尺寸指示器：按住左键拖动无极调节，松手存档
void ResizeHandle(core::dsl::Ui& ui, const std::string& id, float x, float y,
                  float* value, float minValue, float maxValue, const char* prefKey) {
    const float s = Px(15.0f);
    ui.rect(id)
        .x(x).y(y).size(s, s)
        .color(g_theme.panelHi)
        .radius(Px(4.0f))
        .border(1.0f, g_theme.border)
        .states(g_theme.panelHi, g_theme.panelActive, g_theme.selected)
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .onDrag([value, minValue, maxValue](auto& e) {
            const float delta = (float)(e.deltaX + e.deltaY);
            if (delta == 0.0f) return;
            *value = std::clamp(*value * (1.0f + delta / 260.0f), minValue, maxValue);
            app::requestUpdate();
        })
        .onRelease([value, prefKey](auto&, auto&) { SaveLayoutPref(prefKey, *value); })
        .build();
    for (int i = 0; i < 3; ++i) {   // 抓握纹
        ui.rect(id + ".g" + std::to_string(i))
            .x(x + s * 0.26f + (float)i * s * 0.19f).y(y + s * 0.3f)
            .size(Px(1.5f), s * 0.4f)
            .color(g_theme.textMut)
            .radius(Px(1.0f))
            .build();
    }
}

// 鼠标面板：左右键/中键/侧键/滚轮按次数着色（与键盘共用热度语义与悬浮详情）
void DrawMousePanel(core::dsl::Ui& ui, float x, float y, float w, float h) {
    const auto tk = CurrentTheme();
    ui.stack("mouse.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            const float pad = Px(7.0f);
            const float bodyW = w - pad * 2.0f;
            const float bodyH = h - pad * 2.0f;
            ui.rect("mouse.body")
                .x(pad).y(pad).size(bodyW, bodyH)
                .color(g_theme.idleKey)
                .radius(std::min(bodyW, bodyH) * 0.42f)
                .border(1.0f, g_theme.idleEdge)
                .build();

            auto button = [&](const std::string& id, uint8_t vk,
                              float bx, float by, float bw, float bh, float radiusK) {
                const long c = (vk < 256) ? g_stats.counts[vk] : 0;
                double t = g_maxKey > 1 ? (double)c / (double)g_maxKey : 0.0;
                if (c > 0 && t < 0.08) t = 0.08;
                const core::Color fill = HeatColor(t);
                ui.rect(id)
                    .x(bx).y(by).size(bw, bh)
                    .color(fill)
                    .radius(std::min(bw, bh) * radiusK)
                    .states(fill,
                            core::mixColor(fill, Hex(0xFFFFFF), 0.15f),
                            core::mixColor(fill, Hex(0xFFFFFF), 0.25f))
                    .instantStates()
                    .transition(Motion())
                    .animate(core::AnimProperty::Color)
                    .build();
                components::tooltip(ui, id + ".tip")
                    .theme(tk)
                    .source(id)
                    .value(Utf8(StatName(vk)) + " " + WithCommas(c) + " 次")
                    .anchor(bx + bw * 0.5f, by)
                    .bounds(w, h)
                    .style(components::TooltipStyle(tk))
                    .zIndex(300)
                    .build();
            };

            const float gap = Px(2.0f);
            const float topH = bodyH * 0.40f;
            const float halfW = (bodyW - gap) * 0.5f;
            // 左键 / 右键
            button("mouse.l", kMouseLeft, pad, pad, halfW, topH, 0.30f);
            button("mouse.r", kMouseRight, pad + halfW + gap, pad, halfW, topH, 0.30f);
            // 中键 + 滚轮（中缝）
            const float midW = std::max(Px(9.0f), bodyW * 0.17f);
            const float midX = pad + (bodyW - midW) * 0.5f;
            button("mouse.m", kMouseMiddle, midX, pad + Px(1.0f), midW, bodyH * 0.20f, 0.40f);
            const float wheelH = bodyH * 0.22f;
            const float wheelY = pad + bodyH * 0.30f;
            ui.rect("mouse.wheel")
                .x(midX + midW * 0.2f).y(wheelY)
                .size(midW * 0.6f, wheelH)
                .color(HeatColor((g_maxKey > 1)
                                     ? std::max(0.08, (double)std::max(g_stats.counts[kWheelUp],
                                                                       g_stats.counts[kWheelDown]) / (double)g_maxKey)
                                     : 0.0))
                .radius(midW * 0.25f)
                .states(g_theme.panelHi, g_theme.panelActive, g_theme.selected)
                .instantStates()
                .transition(Motion())
                .animate(core::AnimProperty::Color)
                .build();
            components::tooltip(ui, "mouse.wheel.tip")
                .theme(tk)
                .source("mouse.wheel")
                .value("滚轮 上 " + WithCommas(g_stats.counts[kWheelUp]) +
                       " / 下 " + WithCommas(g_stats.counts[kWheelDown]) + " 次")
                .anchor(midX + midW * 0.5f, wheelY)
                .bounds(w, h)
                .style(components::TooltipStyle(tk))
                .zIndex(300)
                .build();
            // 侧键 X1 / X2（机身左侧两条）
            const float sideW = std::max(Px(7.0f), bodyW * 0.13f);
            button("mouse.x1", kMouseX1, pad + bodyW * 0.04f, pad + bodyH * 0.50f,
                   sideW, bodyH * 0.16f, 0.35f);
            button("mouse.x2", kMouseX2, pad + bodyW * 0.04f, pad + bodyH * 0.70f,
                   sideW, bodyH * 0.16f, 0.35f);
        })
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
               s_top10Open ? "隐藏按键列表" : "显示按键列表", false,
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
    EnsureLayoutPrefs();
    const float top = ContentTop();
    // 右侧按键列表开启且窗口足够宽时预留；窄窗口自动隐藏
    const bool rail = s_top10Open && screen.width >= 1000.0f;
    const float railW = rail ? Px(226.0f) * s_listScale + Px(38.0f) : 0.0f;
    const float availW = screen.width - railW - Px(56.0f);
    const float availH = screen.height - top - Px(70.0f);
    const float mouseW = Px(76.0f) * s_mouseScale;
    const float mouseH = Px(126.0f) * s_mouseScale;
    const float mouseGap = Px(26.0f);

    // 键盘单位：随可用空间自适应，再乘用户系数（指示器无极调节）
    auto fitUnit = [](float w, float h) {
        if (w <= 0.0f || h <= 0.0f) return 0.0f;
        return std::clamp(std::min(w / 24.0f, h / 6.0f) * s_kbScale, Px(11.0f), Px(80.0f));
    };
    // 先尝试鼠标并排在键盘右侧；键盘被压得太小则把鼠标放到键盘下方
    float u = fitUnit(availW - mouseW - mouseGap, availH);
    const bool sideBySide = u >= Px(22.0f);
    if (!sideBySide) u = fitUnit(availW, availH - mouseH - mouseGap);
    if (u <= Px(11.0f) && availH <= mouseH + mouseGap) u = fitUnit(availW, availH);   // 高度极窄
    u = std::max(u, Px(9.0f));

    const float kbW = u * 24.0f, kbH = u * 6.0f;
    const float totalW = kbW + (sideBySide ? mouseGap + mouseW : 0.0f);
    const float kx = std::max(Px(14.0f), (screen.width - railW - totalW) * 0.5f);
    const float ky = top + Px(12.0f);
    const float gap = 2.0f;

    // 键盘面板底
    const float px = kx - Px(14.0f), py = ky - Px(14.0f);
    const float pw = kbW + Px(28.0f), ph = kbH + Px(28.0f);
    ui.rect("heat.panel")
        .x(px).y(py).size(pw, ph)
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
    // 键盘盒尺寸指示器（右下角外侧）
    ResizeHandle(ui, "heat.handle", px + pw + Px(5.0f), py + ph + Px(5.0f),
                 &s_kbScale, 0.35f, 2.0f, "kb");

    // 鼠标：并排在右侧，或放到键盘下方
    const float mx = sideBySide ? (kx + kbW + mouseGap) : (kx + (kbW - mouseW) * 0.5f);
    const float my = sideBySide ? (ky + kbH - mouseH) : (ky + kbH + mouseGap);
    DrawMousePanel(ui, mx, my, mouseW, mouseH);
    ResizeHandle(ui, "mouse.handle", mx + mouseW + Px(5.0f), my + mouseH + Px(5.0f),
                 &s_mouseScale, 0.4f, 3.0f, "mouse");

    // 图例：8 段渐变（放在最下方元素之下）
    const float ly = (sideBySide ? (ky + kbH) : (my + mouseH)) + Px(26.0f);
    const float lx = kx + kbW - 8.0f * Px(22.0f) - Px(46.0f);
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

// 右侧按键列表：全部有记录的按键（含鼠标/滚轮）按次数降序，内容超出高度即自动出滚动条
void DrawKeyList(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!(s_top10Open && screen.width >= 1000.0f)) return;
    EnsureLayoutPrefs();

    const float w = Px(226.0f) * s_listScale;
    const float x = screen.width - Px(24.0f) - w;
    const float y = ContentTop();
    const float h = screen.height - y - Px(24.0f);

    ui.rect("list.panel")
        .x(x).y(y).size(w, h)
        .color(g_theme.panel)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .build();
    ui.text("list.title")
        .x(x + Px(16.0f)).y(y + Px(14.0f)).size(w - Px(96.0f), Px(26.0f))
        .text("按键计数")
        .fontSize(Px(20.0f)).lineHeight(Px(24.0f))
        .color(g_theme.text)
        .build();
    MiniButton(ui, "list.collapse", x + w - Px(76.0f), y + Px(10.0f), Px(60.0f), Px(30.0f),
               "收起", false, [] { s_top10Open = false; app::requestUpdate(); });

    const float listX = x + Px(12.0f);
    const float listY = y + Px(48.0f);
    const float listW = w - Px(24.0f);
    const float listH = h - Px(60.0f);
    const float rowH = Px(30.0f);

    components::scrollView(ui, "list.scroll")
        .x(listX).y(listY)
        .size(listW, listH)
        .gap(Px(4.0f))
        .step(rowH)
        .scrollbarWidth(Px(8.0f))
        .scrollbarGap(Px(4.0f))
        .theme(CurrentTheme())
        .transition(Motion())
        .content([&](core::dsl::Ui& cui, float contentW, float) {
            if (g_keyHist.empty()) {
                cui.text("list.empty")
                    .size(contentW, Px(24.0f))
                    .text("暂无数据，去打几个字吧")
                    .fontSize(Px(14.0f)).lineHeight(Px(20.0f))
                    .color(g_theme.textMut)
                    .build();
                return;
            }
            const int cnt = (int)g_keyHist.size();
            for (int k = 0; k < cnt; ++k) {
                const TopEntry& e = g_keyHist[(size_t)(cnt - 1 - k)];   // 降序
                const int rank = k + 1;
                const std::string id = "list.row." + std::to_string(rank);
                cui.stack(id)
                    .size(contentW, rowH)
                    .content([&] {
                        cui.text(id + ".name")
                            .x(0.0f).y(0.0f).size(contentW * 0.62f, rowH)
                            .text(std::to_string(rank) + ". " + e.name)
                            .fontSize(Px(16.0f)).lineHeight(rowH)
                            .color(g_theme.text)
                            .build();
                        cui.text(id + ".count")
                            .x(contentW * 0.62f).y(0.0f).size(contentW * 0.38f, rowH)
                            .text(WithCommas(e.count))
                            .fontSize(Px(16.0f)).lineHeight(rowH)
                            .color(g_theme.textMut)
                            .horizontalAlign(core::HorizontalAlign::Right)
                            .build();
                        cui.rect(id + ".bar.bg")
                            .x(0.0f).y(rowH - Px(4.0f))
                            .size(contentW, 2.0f)
                            .color(g_theme.idleEdge)
                            .radius(1.0f)
                            .build();
                        cui.rect(id + ".bar")
                            .x(0.0f).y(rowH - Px(4.0f))
                            .size(contentW * e.frac, 2.0f)
                            .color(g_theme.selected)
                            .radius(1.0f)
                            .build();
                    })
                    .build();
            }
        })
        .build();

    // 列表宽度指示器（右下角外侧）
    ResizeHandle(ui, "list.handle", x + w + Px(5.0f), y + h + Px(5.0f),
                 &s_listScale, 0.6f, 2.0f, "list");
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
                    // 立即生效（拖动跟手）；落盘由 TickFontScale 防抖
                    RequestFontCustom(kFontScaleMin + v * (kFontScaleMax - kFontScaleMin));
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
