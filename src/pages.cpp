// 页面绘制实现：头部 / 控制行 / 主页看板 / 热力图页 / 按键计数列表。
// 设置页、主题页、直方图页与两个弹窗已按页面拆到 pages_*.cpp；
// 跨页面共享的小构件与状态在 pages_common.h/.cpp；
// 热力着色的归一化（键盘/鼠标点击/滚轮三组峰值）在 heatnorm.cpp。
#include "pages.h"
#include "pages_common.h"
#include "theme.h"
#include "state.h"
#include "heatnorm.h"
#include "ui_util.h"
#include "fontscale.h"
#include "layout.h"
#include "components/components.h"
#include "timeutil.h"
#include "storage.h"
#include "hook.h"
#include "win/adminmode.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace app {

namespace {

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

// 叠放状态（带滞回，避免拖动尺寸时在并排/叠放间来回跳）
bool s_heatStacked = false;
// 按键筛选（键盘/鼠标/全部/分开）已统一为全局的 g_keyFilter（见 state.h）：
// 热力图着色、右侧按键计数列表、直方图页分布图三处共用同一个值。

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
// ── 实时按键状态（记录进程写入共享内存，GUI 只读） ──
static bool KeyPressedNow(unsigned char vk) {
    const unsigned char* st = SharedKeyState();
    if (!st || !st[vk]) return false;
    // state 前面是 uint32 tick：超过 1 秒没有新事件则视为过期（防 UP 丢失卡在按下态）
    const unsigned long tick = *reinterpret_cast<const volatile unsigned long*>(st - 4);
    return (GetTickCount() - tick) < 1000;
}

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
                // 归一化随筛选模式：键盘/鼠标/分开按各自组峰值，全部共用全局峰值
                double t = HeatNorm(vk, c);
                if (c > 0 && t < 0.08) t = 0.08;
                core::Color fill = HeatColor(t);
                // 实时按下反馈：物理按下时键面向主题色亮化（按住期间持续保持）
                const bool pressed = KeyPressedNow(vk);
                if (pressed) fill = core::mixColor(fill, g_theme.selected, 0.55f);
                ui.rect(id)
                    .x(bx).y(by).size(bw, bh)
                    .color(fill)
                    .radius(std::min(bw, bh) * radiusK)
                    .border(pressed ? 1.5f : 0.0f, g_theme.selected)
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
                        .color(c > 0 || pressed ? Hex(0xFFFFFF) : g_theme.textMut)
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
            // 中键上方 = 滚轮上滑（^），下方 = 下滑（v）——中间不再有额外的方形键
            const float upTop = pad + Px(1.0f);
            button("mouse.wup", kWheelUp, midX, upTop, midW, keyH, 0.35f, "^", 0.7f);
            button("mouse.m", kMouseMiddle, midX, upTop + keyH + Px(2.0f), midW, bodyH * 0.16f, 0.40f);
            button("mouse.wdown", kWheelDown, midX, upTop + keyH + Px(2.0f) + bodyH * 0.16f + Px(2.0f),
                   midW, keyH, 0.35f, "v", 0.7f);
            // 侧键 X1 / X2（机身左侧两条）
            const float sideW = std::max(Px(9.0f), bodyW * 0.15f);
            button("mouse.x1", kMouseX1, pad + bodyW * 0.03f, pad + bodyH * 0.62f,
                   sideW, bodyH * 0.13f, 0.35f);
            button("mouse.x2", kMouseX2, pad + bodyW * 0.03f, pad + bodyH * 0.79f,
                   sideW, bodyH * 0.13f, 0.35f);
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
                long count, double t, float boundsW, float boundsH) {
    const auto tk = CurrentTheme();
    const std::string id = "key." + std::to_string(idx);
    const uint8_t vk = kKeys[idx].vk;
    core::Color fill = HeatColor(t);
    core::Color edge = count > 0 ? core::Color{0, 0, 0, 0} : g_theme.idleEdge;
    // 实时按下反馈：物理按下/按住时键面向主题色亮化并描边（与鼠标按键一致）
    const bool pressed = KeyPressedNow(vk);
    if (pressed) {
        fill = core::mixColor(fill, g_theme.selected, 0.55f);
        edge = g_theme.selected;
    }
    ui.rect(id)
        .x(x).y(y).size(w, h)
        .color(fill)
        .radius(std::min(8.0f, h * 0.28f))
        .border(pressed ? 1.5f : 1.0f, edge)
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
            .color(count > 0 || pressed ? Hex(0xFFFFFF) : g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }
    // 悬浮显示真实数据（与鼠标按键的悬浮一致）
    components::tooltip(ui, id + ".tip")
        .theme(tk)
        .source(id)
        .value(Utf8(StatName(vk)) + " " + WithCommas(count) + " 次")
        .anchor(x + w * 0.5f, y)
        .bounds(boundsW, boundsH)
        .style(components::TooltipStyle(tk))
        .zIndex(300)
        .build();
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
    // 管理员模式状态：未提权时在副标题右侧给出一键提权入口
    // （管理员钩子是高完整性级别——管理员窗口、反作弊游戏（Valorant 等）内也能记录）
    if (RunningElevated()) {
        ui.text("hd.admin")
            .x(w - Px(560.0f)).y(Px(60.0f)).size(Px(230.0f), Px(24.0f))
            .text("● 管理员模式运行中（游戏内可记录）")
            .fontSize(Px(14.0f)).lineHeight(Px(24.0f))
            .color(Hex(0x22C55E))
            .build();
    } else {
        MiniButton(ui, "hd.admin", w - Px(560.0f), Px(52.0f), Px(230.0f), Px(32.0f),
                   "⚠ 未提权：游戏内无法记录 → 去开启", false, [] {
                       g_page = 2;                 // 跳转设置页
                       g_adminHighlight = true;
                       g_adminHighlightAt = GetTickCount64();
                       app::requestUpdate();
                   });
    }
    const float bh = Px(34.0f), by = Px(22.0f);
    MiniButton(ui, "hd.top10", w - 306.0f, by, 160.0f, bh,
               s_top10Open ? "隐藏按键列表" : "显示按键列表", false,
               [] { s_top10Open = !s_top10Open; app::requestUpdate(); });
    // 主题切换已独立成"主题"页，这里只留一个快捷入口
    MiniButton(ui, "hd.theme", w - 136.0f, by, 108.0f, bh, "主题", false, [] {
        g_page = 3;
        app::requestUpdate();
    });
}

void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float y = ControlsY();
    const float h = Px(34.0f);

    // segmented 组件自身无定位方法，用带位置的 stack 容器承载
    // 横向位置/宽度不随缩放变化（受窗口宽度约束），只缩放高度
    ui.stack("ctrl.page")
        .x(28.0f).y(y).size(340.0f, h)
        .content([&] {
            components::segmented(ui, "seg.page")
                .size(340.0f, h)
                .items({"热力图", "直方图", "设置", "主题"})
                .selected(g_page)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_page = v; app::requestUpdate(); })
                .build();
        })
        .build();

    ui.stack("ctrl.range")
        .x(380.0f).y(y).size(360.0f, h)
        .content([&] {
            components::segmented(ui, "seg.range")
                .size(360.0f, h)
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

    MiniButton(ui, "btn.from", 750.0f, y, 112.0f, h, "从 " + ymdStr(g_pendingFrom),
               false, [] { g_fromOpen.set(!g_fromOpen.get()); });
    MiniButton(ui, "btn.to", 870.0f, y, 112.0f, h, "至 " + ymdStr(g_pendingTo),
               false, [] { g_toOpen.set(!g_toOpen.get()); });
    MiniButton(ui, "btn.apply", 990.0f, y, 72.0f, h, "应用", true, [] {
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
    float boardY = 0.0f, boardH = 0.0f;   // 主页看板（键鼠区上方横条）
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
    g.boardY = ContentTop() + Px(4.0f);
    g.boardH = Px(64.0f);
    g.boxX = g.contentX;
    g.boxY = g.boardY + g.boardH + Px(10.0f);
    g.boxW = boxW;
    g.boxH = std::max(Px(160.0f), screen.height - g.boxY - Px(72.0f));
    g.listX = g.contentX + boxW + g.gapX;
    g.listY = g.boxY;
    g.listW = g.hasList ? std::max(Px(140.0f), g.contentX + g.contentW - g.listX) : 0.0f;
    g.listH = g.boxH;
    return g;
}

// ────────────────────────── 主页看板（键鼠区上方横条） ──────────────────────────

void DrawBoard(core::dsl::Ui& ui, const eui::Screen& screen) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;
    const HeatGeometry geo = HeatGeom(screen);
    const float x = geo.contentX, y = geo.boardY, w = geo.contentW, h = geo.boardH;

    ui.rect("board.bg")
        .x(x).y(y).size(w, h)
        .color(tk.surface)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 12.0f, 3.0f, 0.14f, 0.08f))
        .build();

    const TodayBreakdown td = StorageTodayBreakdown();
    const StorageInfo info = StorageDescribe();
    const long activeDays = StorageActiveDayCount();
    const long usedDays = info.firstYmd ? DayDiff(TodayLocal(), info.firstYmd) + 1 : 0;
    const long score = td.keyboard + td.mouseClicks + (long)(td.wheel * 0.1);

    struct Card { const char* label; std::string value; };
    const Card cards[6] = {
        {"活跃分数",  std::to_string(score)},
        {"今日键盘",  std::to_string(td.keyboard)},
        {"今日鼠标",  std::to_string(td.mouseClicks)},
        {"滚轮格数",  std::to_string(td.wheel)},
        {"使用天数",  std::to_string(usedDays)},
        {"活跃天数",  std::to_string(activeDays)},
    };

    const int n = 6;
    const float pad = Px(14.0f);
    const float gapC = Px(10.0f);
    const float cw = (w - pad * 2.0f - gapC * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
        const float cx = x + pad + (cw + gapC) * i;
        ui.rect("board.c" + std::to_string(i))
            .x(cx).y(y + Px(9.0f)).size(cw, h - Px(18.0f))
            .color(g_theme.panel)
            .radius(Px(8.0f))
            .border(1.0f, i == 0 ? components::theme::withOpacity(g_theme.selected, 0.55f)
                                 : g_theme.border)
            .build();
        ui.text("board.l" + std::to_string(i))
            .x(cx + Px(10.0f)).y(y + Px(14.0f)).size(cw - Px(20.0f), Px(18.0f))
            .text(cards[i].label)
            .fontSize(m.typography.caption)
            .lineHeight(Px(18.0f))
            .color(g_theme.textMut)
            .build();
        ui.text("board.v" + std::to_string(i))
            .x(cx + Px(10.0f)).y(y + Px(32.0f)).size(cw - Px(20.0f), Px(24.0f))
            .text(cards[i].value)
            .fontSize(m.typography.title)
            .lineHeight(Px(24.0f))
            .color(i == 0 ? g_theme.selected : g_theme.text)
            .build();
    }
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

    // 用户设定尺寸（各自最小/最大钳制）
    const float kbWantRaw = std::clamp(kbRefW * s_kbScale, kbMinW, kbMaxW);
    const float mouseWantRaw = std::clamp(mouseRefW * s_mouseScale, mouseMinW, mouseMaxW);

    // 叠放判定带滞回：让位方到最小值仍放不下才进入；两侧按用户值都能放下（留 10% 余量）才退出。
    // 这样在叠放状态下拖动某一方不会立刻跳回并排。
    {
        const float wantSum = kbWantRaw + gapM + mouseWantRaw;
        if (!s_heatStacked) {
            const float drivenWant = (s_resizeDriver == 2) ? mouseWantRaw : kbWantRaw;
            const float otherMin = (s_resizeDriver == 2) ? kbMinW : mouseMinW;
            if (drivenWant + gapM + otherMin > totalW) s_heatStacked = true;
        } else if (wantSum <= totalW * 0.9f) {
            s_heatStacked = false;
        }
    }
    const bool stacked = s_heatStacked;

    // 并排：被调方保持，另一方吸收剩余空间；叠放：两侧互不影响
    float kbWant = std::min(kbWantRaw, innerH * 4.0f);
    float mouseWant = std::min(mouseWantRaw, innerH / kMouseAspect);

    float kbW = 0.0f, mouseW = 0.0f;
    if (stacked) {
        kbW = std::min(kbWantRaw, innerW);
        mouseW = std::min(mouseWantRaw, innerW);
    } else if (s_resizeDriver == 2) {             // 刚调鼠标：鼠标保持，键盘吸收剩余
        mouseW = mouseWant;
        kbW = std::min(totalW - mouseW, kbMaxW);
        if (kbW < kbMinW) kbW = kbMinW;
    } else {                                      // 键盘主导（含初始）
        kbW = kbWant;
        // 鼠标吸收剩余空间，但不超过"键盘高度 × 0.95"的视觉比例（避免鼠标比键盘还高）
        const float mouseCap = std::max(mouseMinW, kbW * 0.95f / (4.0f * kMouseAspect));
        mouseW = std::min({totalW - kbW, mouseMaxW, mouseCap});
        if (mouseW < mouseMinW) mouseW = mouseMinW;
    }

    float mouseH = mouseW * kMouseAspect;
    if (stacked) {                                // 叠放：保留用户尺寸，超出盒高交给滚动条
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
            // 归一化随筛选模式（见 app::HeatNorm）：三组可各自独立求峰值
            double t = HeatNorm(k.vk, cnt);
            if (cnt > 0 && t < 0.08) t = 0.08;
            DrawKeycap(c, i, kbX + k.x * u + gap, k.y * u + gap,
                       k.w * u - gap * 2.0f, k.h * u - gap * 2.0f, cnt, t, innerW, drawH);
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
    // 反转模式下色块序列经 HeatColor 自动镜像（左=红=少值色，右=蓝=多值色），
    // "少/多"文字**保持不变**——它标注的是当前映射下的颜色位置，语义仍然成立。
    const bool inv = HeatInverted();
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
    // 反转开关（图例右侧）：高频显低频色、低频显高频色
    MiniButton(ui, "legend.invert", lx + legendW + Px(24.0f), ly - Px(9.0f),
               Px(110.0f), Px(32.0f), inv ? "取消反转" : "反转颜色", false, [] {
                   SetHeatInverted(!HeatInverted());
               });
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

    // 按键筛选：与热力图着色、直方图页共用同一个全局值（切换即时生效）
    const float filtY = y + Px(48.0f);
    const float filtW = std::max(Px(120.0f), w - Px(24.0f));
    const bool showFilter = filtW >= Px(190.0f);   // 面板过窄时隐藏，保持"全部"
    if (showFilter) {
        ui.stack("list.filter")
            .x(x + Px(12.0f)).y(filtY).size(filtW, Px(32.0f))
            .content([&] {
                components::segmented(ui, "seg.listfilter")
                    .size(filtW, Px(32.0f))
                    .items({"键盘", "鼠标", "全部", "分开"})
                    .selected(g_keyFilter)
                    .theme(CurrentTheme())
                    .transition(Motion())
                    .onChange([](int v) { g_keyFilter = v; app::requestUpdate(); })
                    .build();
            })
            .build();
    }

    const float listX = x + Px(12.0f);
    const float listY = filtY + (showFilter ? Px(40.0f) : Px(2.0f));
    const float listW = w - Px(24.0f);
    const float listH = std::max(Px(60.0f), y + h - Px(12.0f) - listY);
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
            // 依筛选构建行：0=键盘 1=鼠标 2=全部 3=分开（键盘区在上、鼠标区在下）
            struct Row { const TopEntry* e; std::string header; };
            std::vector<Row> rows;
            auto pushDesc = [&](bool mouseGroup) {
                for (int k = (int)g_keyHist.size() - 1; k >= 0; --k) {
                    const TopEntry& e = g_keyHist[(size_t)k];
                    if (e.count <= 0 || e.isMouse != mouseGroup) continue;
                    rows.push_back({&e, std::string()});
                }
            };
            if (g_keyFilter == 3) {
                rows.push_back({nullptr, "键盘"});
                pushDesc(false);
                rows.push_back({nullptr, "鼠标"});
                pushDesc(true);
            } else if (g_keyFilter == 2) {
                for (int k = (int)g_keyHist.size() - 1; k >= 0; --k) {
                    const TopEntry& e = g_keyHist[(size_t)k];
                    if (e.count <= 0) continue;
                    rows.push_back({&e, std::string()});
                }
            } else {
                pushDesc(g_keyFilter == 1);
            }

            if (rows.empty()) {
                cui.text("list.empty")
                    .size(contentW, Px(24.0f))
                    .text("暂无数据，去打几个字吧")
                    .fontSize(Px(14.0f)).lineHeight(Px(20.0f))
                    .color(g_theme.textMut)
                    .build();
                return;
            }
            int rank = 0;
            for (size_t k = 0; k < rows.size(); ++k) {
                const Row& r = rows[k];
                const std::string id = "list.row." + std::to_string(k);
                if (r.e == nullptr) {                       // 分区标题
                    cui.text(id + ".header")
                        .size(contentW, Px(24.0f))
                        .text(r.header)
                        .fontSize(Px(15.0f)).lineHeight(Px(22.0f))
                        .color(g_theme.textMut)
                        .build();
                    rank = 0;
                    continue;
                }
                const TopEntry& e = *r.e;
                ++rank;
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
                            // 条宽与热力图同一套归一化：分开模式下两段各自满格，
                            // 键盘/鼠标/全部模式也随筛选即时变化
                            .size(contentW * (float)HeatNorm(e.vk, e.count), 2.0f)
                            .color(g_theme.selected)
                            .radius(1.0f)
                            .build();
                    })
                    .build();
            }
        })
        .build();
}

} // namespace app
