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

// ────────────────── 版面尺寸（外盒/列表边界可无极调节，持久化） ──────────────────

float s_kbScale = 1.0f;      // 键盘尺寸系数（相对盒内自适应值）
float s_mouseScale = 1.0f;   // 鼠标尺寸系数
float s_split = 0.68f;       // 外盒 / 按键计数区 的边界位置（内容宽度的比例）
bool  s_layoutLoaded = false;

void EnsureLayoutPrefs() {
    if (s_layoutLoaded) return;
    s_layoutLoaded = true;
    s_kbScale    = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "kb", "1.0").c_str()), 0.6f, 1.6f);
    s_mouseScale = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "mouse", "1.0").c_str()), 0.6f, 1.8f);
    s_split      = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "split", "0.68").c_str()), 0.30f, 0.90f);
}

void SaveLayoutPref(const char* key, float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", value);
    PrefSetValue(L"ui-layout.txt", key, buf);
}

// 最近被调节的一方：1=键盘 2=鼠标。被调方保持尺寸，另一方吸收剩余空间。
int s_resizeDriver = 0;

// 内盒右下角外侧的尺寸指示器：按住左键拖动无极调节（松手存档）
void ResizeHandle(core::dsl::Ui& ui, const std::string& id, float x, float y,
                  float* value, float minValue, float maxValue, const char* prefKey,
                  int driverId) {
    const float s = Px(15.0f);
    ui.rect(id)
        .x(x).y(y).size(s, s)
        .color(g_theme.panelHi)
        .radius(Px(4.0f))
        .border(1.0f, g_theme.border)
        .states(g_theme.panelHi, g_theme.panelActive, g_theme.selected)
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .onDrag([value, minValue, maxValue, driverId](auto& e) {
            s_resizeDriver = driverId;   // 本次调节由该方主导
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

// 鼠标盒子：面板 + 机身，左右键/中键/侧键/滚轮（含上滑 ^ 下滑 v 键）按次数着色
void DrawMousePanel(core::dsl::Ui& ui, float x, float y, float w, float h) {
    const auto tk = CurrentTheme();
    ui.stack("mouse.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            // 鼠标盒子底
            ui.rect("mouse.box")
                .size(w, h)
                .color(g_theme.panelHi)
                .radius(Px(10.0f))
                .border(1.0f, g_theme.border)
                .build();
            const float pad = Px(13.0f);
            const float bodyW = w - pad * 2.0f;
            const float bodyH = h - pad * 2.0f;
            ui.rect("mouse.body")
                .x(pad).y(pad).size(bodyW, bodyH)
                .color(g_theme.idleKey)
                .radius(std::min(bodyW, bodyH) * 0.42f)
                .border(1.0f, g_theme.idleEdge)
                .build();

            auto button = [&](const std::string& id, uint8_t vk,
                              float bx, float by, float bw, float bh, float radiusK,
                              const std::string& glyph = std::string(), float glyphK = 0.5f) {
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
                if (!glyph.empty()) {
                    ui.text(id + ".glyph")
                        .x(bx).y(by).size(bw, bh)
                        .text(glyph)
                        .fontSize(std::min(bw, bh) * glyphK)
                        .lineHeight(bh)
                        .color(c > 0 ? Hex(0xFFFFFF) : g_theme.textMut)
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                }
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
            button("mouse.l", kMouseLeft, pad, pad, halfW, topH, 0.30f);
            button("mouse.r", kMouseRight, pad + halfW + gap, pad, halfW, topH, 0.30f);

            const float midW = std::max(Px(11.0f), bodyW * 0.19f);
            const float midX = pad + (bodyW - midW) * 0.5f;
            const float keyH = std::max(Px(8.0f), bodyH * 0.13f);
            button("mouse.m", kMouseMiddle, midX, pad + Px(1.0f), midW, bodyH * 0.16f, 0.40f);
            // 滚轮上滑键（^）→ 滚轮本体 → 下滑键（v）
            const float wUpTop = pad + bodyH * 0.22f;
            button("mouse.wup", kWheelUp, midX, wUpTop, midW, keyH, 0.35f, "^", 0.7f);
            const float wheelY = wUpTop + keyH + Px(2.0f);
            const float wheelH = std::max(Px(8.0f), bodyH * 0.11f);
            ui.rect("mouse.wheel")
                .x(midX + midW * 0.28f).y(wheelY)
                .size(midW * 0.44f, wheelH)
                .color(g_theme.panelHi)
                .radius(midW * 0.2f)
                .border(1.0f, g_theme.idleEdge)
                .build();
            button("mouse.wdown", kWheelDown, midX, wheelY + wheelH + Px(2.0f),
                   midW, keyH, 0.35f, "v", 0.7f);
            // 侧键 X1 / X2（机身左侧两条）
            const float sideW = std::max(Px(9.0f), bodyW * 0.15f);
            button("mouse.x1", kMouseX1, pad + bodyW * 0.03f, pad + bodyH * 0.55f,
                   sideW, bodyH * 0.15f, 0.35f);
            button("mouse.x2", kMouseX2, pad + bodyW * 0.03f, pad + bodyH * 0.74f,
                   sideW, bodyH * 0.15f, 0.35f);
        })
        .build();
}

// 外盒与按键计数区之间的边界：按住拖动无极调节（松手存档）
void SplitDivider(core::dsl::Ui& ui, float x, float y, float h, float contentX, float contentW) {
    const float w = Px(10.0f);
    ui.rect("heat.divider")
        .x(x).y(y).size(w, h)
        .color(g_theme.panelHi)
        .radius(Px(5.0f))
        .border(1.0f, g_theme.border)
        .states(g_theme.panelHi, g_theme.panelActive, g_theme.selected)
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .onDrag([contentX, contentW](auto& e) {
            const float delta = (float)e.deltaX;
            if (delta == 0.0f || contentW <= 0.0f) return;
            s_split = std::clamp(s_split + delta / contentW, 0.30f, 0.90f);
            app::requestUpdate();
        })
        .onRelease([](auto&, auto&) { SaveLayoutPref("split", s_split); })
        .build();
    for (int i = 0; i < 3; ++i) {   // 抓握纹
        ui.rect("heat.divider.g" + std::to_string(i))
            .x(x + w * 0.32f).y(y + h * 0.5f - Px(16.0f) + (float)i * Px(12.0f))
            .size(w * 0.36f, Px(2.0f))
            .color(g_theme.textMut)
            .radius(Px(1.0f))
            .build();
    }
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

// 外盒 / 列表几何（DrawHeatPage 与 DrawKeyList 共用，避免边界公式分叉）
struct HeatGeometry {
    float boxX = 0.0f, boxY = 0.0f, boxW = 0.0f, boxH = 0.0f;
    float listX = 0.0f, listY = 0.0f, listW = 0.0f, listH = 0.0f;
    float contentX = 0.0f, contentW = 0.0f, gapX = 0.0f;
    bool  hasList = false;
};

HeatGeometry HeatGeom(const eui::Screen& screen) {
    EnsureLayoutPrefs();
    HeatGeometry g;
    g.contentX = Px(28.0f);
    g.contentW = screen.width - Px(56.0f);
    g.gapX = Px(22.0f);
    g.hasList = s_top10Open && screen.width >= Px(900.0f);
    const float listMinW = Px(210.0f);
    float boxW = g.hasList ? std::clamp(g.contentW * s_split, Px(380.0f),
                                       std::max(Px(380.0f), g.contentW - listMinW - g.gapX))
                           : g.contentW;
    boxW = std::max(boxW, Px(240.0f));
    g.boxX = g.contentX;
    g.boxY = ContentTop() + Px(12.0f);
    g.boxW = boxW;
    g.boxH = std::max(Px(160.0f), screen.height - g.boxY - Px(72.0f));
    g.listX = g.contentX + boxW + g.gapX;
    g.listY = g.boxY;
    g.listW = g.hasList ? std::max(Px(140.0f), g.contentX + g.contentW - g.listX) : 0.0f;
    g.listH = g.boxH;
    return g;
}

void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const HeatGeometry geo = HeatGeom(screen);
    const float contentX = geo.contentX, contentW = geo.contentW, gapX = geo.gapX;
    const float boxX = geo.boxX, boxY = geo.boxY, boxW = geo.boxW, boxH = geo.boxH;
    const bool rail = geo.hasList;

    ui.rect("heat.box")
        .x(boxX).y(boxY).size(boxW, boxH)
        .color(g_theme.panel)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .build();

    // ── 盒内排布：键盘盒子 + 鼠标盒子 ──
    const float pad = Px(14.0f);
    const float innerW = std::max(Px(60.0f), boxW - pad * 2.0f);
    const float innerH = std::max(Px(60.0f), boxH - pad * 2.0f);
    const float gapM = Px(20.0f);
    const float kbMinW = 24.0f * Px(9.0f);        // 每键最小 9px
    const float kbMaxW = 24.0f * Px(72.0f);
    const float mouseMinW = Px(56.0f);
    const float mouseMaxW = Px(200.0f);
    const float kMouseAspect = 1.62f;             // 鼠标盒子 高/宽

    // 参考尺寸：默认布局下的自然值，用户系数在此之上升降。
    // 鼠标参考宽度按"键盘高度的一半左右"取，保证与键盘视觉比例协调（而不是随盒宽膨胀）
    const float kbRefW0 = std::clamp(std::min(innerW - Px(140.0f), innerH * 4.0f), kbMinW, kbMaxW);
    const float mouseRefW = std::clamp(kbRefW0 / 4.0f * 0.78f / kMouseAspect, mouseMinW, mouseMaxW);
    const float kbRefW = std::clamp(std::min(innerW - mouseRefW - gapM, innerH * 4.0f),
                                    kbMinW, kbMaxW);
    const float totalW = std::max(Px(40.0f), innerW - gapM);

    // 用户设定尺寸（各自最小/最大钳制），并限制不超过盒高
    float kbWant = std::min(std::clamp(kbRefW * s_kbScale, kbMinW, kbMaxW), innerH * 4.0f);
    float mouseWant = std::min(std::clamp(mouseRefW * s_mouseScale, mouseMinW, mouseMaxW),
                               innerH / kMouseAspect);

    float kbW = 0.0f, mouseW = 0.0f;
    bool stacked = false;
    if (s_resizeDriver == 2) {                    // 刚调鼠标：鼠标保持，键盘吸收剩余
        mouseW = mouseWant;
        kbW = std::min(totalW - mouseW, kbMaxW);
        if (kbW < kbMinW) { kbW = kbMinW; stacked = true; }   // 键盘已到下限仍放不下 → 叠放
    } else {                                      // 键盘主导（含初始）
        kbW = kbWant;
        // 鼠标吸收剩余空间，但不超过"键盘高度 × 0.95"的视觉比例（避免鼠标比键盘还高）
        const float mouseCap = std::max(mouseMinW, kbW * 0.95f / (4.0f * kMouseAspect));
        mouseW = std::min({totalW - kbW, mouseMaxW, mouseCap});
        if (mouseW < mouseMinW) { mouseW = mouseMinW; stacked = true; }
    }

    float mouseH = mouseW * kMouseAspect;
    if (stacked) {                                 // 叠放：保留用户尺寸，超出盒高交给滚动条
        kbW = std::min(std::clamp(kbRefW * s_kbScale, kbMinW, kbMaxW), innerW);
        mouseW = std::min(std::clamp(mouseRefW * 1.25f * s_mouseScale, mouseMinW, mouseMaxW), innerW);
        mouseH = mouseW * kMouseAspect;
    }
    const float u = std::max(Px(6.0f), kbW / 24.0f);
    const float kbH = 6.0f * u;
    kbW = 24.0f * u;
    const float contentH = stacked ? (kbH + gapM + mouseH) : std::max(kbH, mouseH);
    const bool needScroll = contentH > innerH;
    const float drawH = needScroll ? contentH : innerH;

    // 位置：组居中；并排=键盘左鼠标右，叠放=键盘上鼠标下
    const float groupW = stacked ? std::max(kbW, mouseW) : (kbW + gapM + mouseW);
    const float gx = std::max(0.0f, (innerW - groupW) * 0.5f);
    const float kbX = stacked ? (gx + (groupW - kbW) * 0.5f) : gx;
    const float mouseX = stacked ? (gx + (groupW - mouseW) * 0.5f) : (gx + kbW + gapM);
    const float mouseY = stacked ? (kbH + gapM) : std::max(0.0f, (kbH - mouseH) * 0.5f);

    auto drawContent = [&](core::dsl::Ui& c) {
        const float gap = 2.0f;
        const float kbp = Px(10.0f);   // 键盘盒子内边距
        // 键盘盒子（大盒子内的子盒）
        c.rect("heat.kbbox")
            .x(kbX - kbp).y(-kbp)
            .size(kbW + kbp * 2.0f, kbH + kbp * 2.0f)
            .color(g_theme.panelHi)
            .radius(Px(10.0f))
            .border(1.0f, g_theme.border)
            .build();
        const int n = (int)(sizeof(kKeys) / sizeof(kKeys[0]));
        for (int i = 0; i < n; ++i) {
            const KeyDef& k = kKeys[i];
            long cnt = (k.vk < 256) ? g_stats.counts[k.vk] : 0;
            double t = g_maxKey > 1 ? (double)cnt / (double)g_maxKey : 0.0;
            if (cnt > 0 && t < 0.08) t = 0.08;
            DrawKeycap(c, i, kbX + k.x * u + gap, k.y * u + gap,
                       k.w * u - gap * 2.0f, k.h * u - gap * 2.0f, cnt, t);
        }
        // 键盘盒子尺寸指示器（右下角外侧；钳制在内容范围内，避免被裁剪）
        const float hs = Px(17.0f);
        ResizeHandle(c, "kb.handle",
                     std::min(kbX + kbW + kbp + Px(5.0f), innerW - hs),
                     std::min(kbH + kbp + Px(5.0f), drawH - hs),
                     &s_kbScale, 0.6f, 1.6f, "kb", 1);
        // 鼠标盒子（大盒子内的子盒）
        DrawMousePanel(c, mouseX, mouseY, mouseW, mouseH);
        ResizeHandle(c, "mouse.handle",
                     std::min(mouseX + mouseW + Px(5.0f), innerW - hs),
                     std::min(mouseY + mouseH + Px(5.0f), drawH - hs),
                     &s_mouseScale, 0.6f, 1.8f, "mouse", 2);
    };

    if (needScroll) {
        components::scrollView(ui, "heat.scroll")
            .x(contentX + pad).y(boxY + pad)
            .size(innerW, innerH)
            .gap(0.0f)
            .step(Px(40.0f))
            .scrollbarWidth(Px(9.0f))
            .scrollbarGap(Px(3.0f))
            .theme(CurrentTheme())
            .transition(Motion())
            .content([&](core::dsl::Ui& cui, float cw, float) {
                cui.stack("heat.content")
                    .size(cw, drawH)
                    .content([&] { drawContent(cui); })
                    .build();
            })
            .build();
    } else {
        ui.stack("heat.content")
            .x(contentX + pad).y(boxY + pad)
            .size(innerW, drawH)
            .content([&] { drawContent(ui); })
            .build();
    }

    // ── 边界（可拖动无极调节盒宽 / 列表宽）──
    if (rail) {
        SplitDivider(ui, contentX + boxW + gapX * 0.5f - Px(5.0f), boxY, boxH, contentX, contentW);
    }

    // ── 图例（盒下方居中）──
    const float ly = boxY + boxH + Px(18.0f);
    const float legendW = 8.0f * Px(22.0f) + Px(70.0f);
    const float lx = contentX + boxW * 0.5f - legendW * 0.5f;
    ui.text("legend.lo")
        .x(lx).y(ly - Px(4.0f)).size(Px(32.0f), Px(20.0f))
        .text("少").fontSize(Px(16.0f)).lineHeight(Px(20.0f))
        .color(g_theme.textMut).horizontalAlign(core::HorizontalAlign::Right)
        .build();
    for (int i = 0; i < 8; ++i) {
        ui.rect("legend.sw." + std::to_string(i))
            .x(lx + Px(36.0f) + i * Px(22.0f)).y(ly)
            .size(Px(20.0f), Px(12.0f))
            .color(HeatColor((i + 0.5) / 8.0))
            .radius(3.0f)
            .build();
    }
    ui.text("legend.hi")
        .x(lx + Px(40.0f) + 8 * Px(22.0f)).y(ly - Px(4.0f)).size(Px(26.0f), Px(20.0f))
        .text("多").fontSize(Px(16.0f)).lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();
}

// 右侧按键列表：全部有记录的按键（含鼠标/滚轮）按次数降序，内容超出高度即自动出滚动条
// 位置与宽度由外盒/列表之间的边界（SplitDivider）决定
void DrawKeyList(core::dsl::Ui& ui, const eui::Screen& screen) {
    const HeatGeometry geo = HeatGeom(screen);
    if (!geo.hasList) return;

    const float x = geo.listX, y = geo.listY, w = geo.listW, h = geo.listH;

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
            int shown = 0;
            for (int k = 0; k < cnt; ++k) {
                const TopEntry& e = g_keyHist[(size_t)(cnt - 1 - k)];   // 降序
                if (e.count <= 0) continue;                            // 列表只列有记录的按键
                const int rank = ++shown;
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

                // 键名：条宽放得下就标；放不下时按步长稀疏标注（保证小键盘 1`、2` 等仍可辨认）
                const float estW = (float)e.name.size() * labelFont * 0.62f;
                bool nLabel = slotW >= estW + 4.0f;
                if (!nLabel) {
                    const int stride = std::max(1, (int)std::ceil((estW + 4.0f) / std::max(1.0f, slotW)));
                    nLabel = (i % stride) == 0;
                }
                if (nLabel) {
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
