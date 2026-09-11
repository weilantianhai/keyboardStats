// 页面绘制实现：头部/控制行/热力图页/Top10/直方图页
#include "pages.h"
#include "theme.h"
#include "state.h"
#include "ui_util.h"
#include "fontscale.h"
#include "layout.h"
#include "components/components.h"
#include "timeutil.h"
#include "storage.h"
#include "pref.h"
#include "win/filedialog.h"
#include "win/autostart.h"
#include "win/adminmode.h"
#include "hook.h"

#include <shellapi.h>

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
// 叠放状态（带滞回，避免拖动尺寸时在并排/叠放间来回跳）
bool s_heatStacked = false;
// 按键计数列表的独立筛选：0=键盘 1=鼠标 2=全部 3=分开
int s_listFilter = 2;

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
                double t = g_maxKey > 1 ? (double)c / (double)g_maxKey : 0.0;
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
                   "⚠ 未提权：游戏内无法记录 → 开启", false, [] {
                       if (RelaunchAsAdmin()) {
                           // 新的提权实例接管（coreReady 里等待本进程退出并升级记录进程）
                           Sleep(600);
                           ExitAppNow();
                       }
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
            double t = g_maxKey > 1 ? (double)cnt / (double)g_maxKey : 0.0;
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

    // 列表自己的筛选标签（与直方图筛选互不影响）
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
                    .selected(s_listFilter)
                    .theme(CurrentTheme())
                    .transition(Motion())
                    .onChange([](int v) { s_listFilter = v; app::requestUpdate(); })
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
            if (s_listFilter == 3) {
                rows.push_back({nullptr, "键盘"});
                pushDesc(false);
                rows.push_back({nullptr, "鼠标"});
                pushDesc(true);
            } else if (s_listFilter == 2) {
                for (int k = (int)g_keyHist.size() - 1; k >= 0; --k) {
                    const TopEntry& e = g_keyHist[(size_t)k];
                    if (e.count <= 0) continue;
                    rows.push_back({&e, std::string()});
                }
            } else {
                pushDesc(s_listFilter == 1);
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

            // 筛选：键盘 / 鼠标 / 全部 / 分开（分开=左键盘右鼠标，只改排序不改盒子）
            const float filterW = Px(360.0f), filterH = Px(34.0f);
            ui.stack("keyhist.filter")
                .x(w - titleX - filterW).y(m.typography.control - Px(2.0f)).size(filterW, filterH)
                .content([&] {
                    components::segmented(ui, "seg.keyfilter")
                        .size(filterW, filterH)
                        .items({"键盘", "鼠标", "全部", "分开"})
                        .selected(g_keyFilter)
                        .theme(tk)
                        .transition(Motion())
                        .onChange([](int v) { g_keyFilter = v; app::requestUpdate(); })
                        .build();
                })
                .build();

            // 右上角注解：最多键
            const TopEntry& maxE = g_keyHist.back();
            ui.text("keyhist.max")
                .x(titleX)
                .y(m.typography.control + m.typography.title + Px(4.0f))
                .size(std::max(0.0f, w - titleX * 2.0f), m.control.compact)
                .text("最多：" + maxE.name + " " + WithCommas(maxE.count) + " 次")
                .fontSize(m.typography.label)
                .lineHeight(m.typography.label + m.typography.lineGap)
                .color(g_theme.textMut)
                .build();

            // 绘图区几何与 barChart 相同
            const float plotX = 32.0f;
            const float plotY = 70.0f;
            const float plotW = std::max(1.0f, w - 64.0f);
            const float plotH = std::max(1.0f, h - 78.0f);   // 无横坐标文字，底部空间留给条形
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

            // 依筛选构建显示序列（全部为升序；分开 = 键盘在前、鼠标在后，仍是同一条序列）
            std::vector<const TopEntry*> items;
            for (const TopEntry& e : g_keyHist)                    // g_keyHist 本身按次数升序
                if (g_keyFilter == 0 && e.isMouse) continue;
                else if (g_keyFilter == 1 && !e.isMouse) continue;
                else if (g_keyFilter == 3 && e.isMouse) continue;  // 分开：先只放键盘
                else items.push_back(&e);
            if (g_keyFilter == 3) {
                for (const TopEntry& e : g_keyHist)
                    if (e.isMouse) items.push_back(&e);            // 再追加鼠标 → 排到最右
            }
            if (items.empty()) return;

            const int n = (int)items.size();
            const float slotW = plotW / (float)n;
            // 去掉横坐标文字后条形可以更宽一些
            const float barW = std::min(40.0f, std::max(2.0f, slotW * 0.70f));
            for (int i = 0; i < n; ++i) {
                const TopEntry& e = *items[(size_t)i];
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

// 记录管理状态（设置页）
StorageInfo s_recInfo;
double s_recInfoAt = 0.0;
std::string s_recMsg;
double s_recMsgAt = 0.0;
int s_clearConfirm = 0;   // 0=无 1=第一次确认 2=第二次确认

// 切换数据文件夹：原文件夹里发现数据文件时，先弹窗问是否一起搬走
std::wstring s_pendingDir;    // 待切换的目标文件夹（非空 = 显示确认框）
int s_pendingDirFiles = 0;    // 待搬运的数据文件个数

// 滚动位置（跨帧保持）
eui::Signal<float> s_settingsScroll{0.0f};
eui::Signal<float> s_themeScroll{0.0f};

// 简易滚动容器：内容高度由调用方给出，内容回调每帧只跑一次。
// 不用框架 ScrollView 的自动测量——它会先在一个独立的测量 Ui 里把内容回调再跑一遍，
// 那一遍创建的交互元素会留下按"内容坐标"算的命中框；操作几次之后这些残留命中框会和
// 真实元素抢悬停，鼠标一进组件就乱跳。这里把高度写死，就只构建一次。
float ScrollArea(core::dsl::Ui& ui, const std::string& id, float x, float y, float w, float h,
                 float contentH, eui::Signal<float>& offSignal,
                 const std::function<void(core::dsl::Ui&, float)>& body) {
    const float maxOff = std::max(0.0f, contentH - h);
    const float off = std::clamp(offSignal.get(), 0.0f, maxOff);
    const float bodyW = std::max(Px(120.0f), w - Px(16.0f));   // 右侧留给自绘滚动条
    ui.stack(id)
        .x(x).y(y).size(w, h)
        .clip()
        .scrollState(id, off, maxOff, Px(52.0f))
        .onScrollOffsetChanged([&offSignal](float v) { offSignal.set(v); })
        .content([&] {
            ui.stack(id + ".content")
                .size(bodyW, contentH)
                .scrollContentFrom(id)   // 让子树跟着偏移，并修正命中判定
                .content([&] { body(ui, bodyW); })
                .build();
        })
        .build();
    return off;
}

// 自己画的滚动条（框架内置的那条在本项目主题下不显示）
void ScrollThumb(core::dsl::Ui& ui, const char* id, float x, float y, float w, float viewH,
                 float contentH, float off) {
    if (contentH <= viewH) return;
    const float barW = Px(6.0f);
    const float barX = x + w - barW;
    const float maxOff = contentH - viewH;
    const float thumbH = std::max(Px(48.0f), viewH * (viewH / contentH));
    const float thumbY = y + (viewH - thumbH) * (std::clamp(off, 0.0f, maxOff) / maxOff);
    ui.rect(std::string(id) + ".track")
        .x(barX).y(y).size(barW, viewH)
        .color(components::theme::withOpacity(g_theme.border, 0.35f))
        .radius(barW * 0.5f).build();
    ui.rect(std::string(id) + ".thumb")
        .x(barX).y(thumbY).size(barW, thumbH)
        .color(components::theme::withOpacity(g_theme.textMut, 0.75f))
        .radius(barW * 0.5f).build();
}

// 主题页三个面板的高度（同设置页：按内容固定，不跟窗口高度挂钩）
constexpr float kPalettePanelH = 258.0f;
constexpr float kHeatPanelH    = 196.0f;
constexpr float kCustomPanelH  = 196.0f;

// 两个面板的高度按"内容需要"固定，不随窗口高度压缩：
// 之前按可用高度取比例，窗口一小面板就比内容矮，说明文字会和滑块叠在一起，
// 底部的按钮也会被窗口裁掉。现在改为固定内容高度 + 外层滚动视图。
constexpr float kFontPanelH = 440.0f;   // 含管理员开关行、说明文档行与框架署名
constexpr float kRecPanelH  = 276.0f;
constexpr float kPanelGap   = 12.0f;

// 字体设置面板：画在"滚动内容坐标系"里（原点 = 内容左上角，宽 = w）
static void DrawFontPanel(core::dsl::Ui& ui, float w, float y,
                          const components::theme::ThemeColorTokens& tk, float screenWidth) {
    const auto& m = tk.metrics;
    const float x = 0.0f;
    const float h = Px(kFontPanelH);

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

    // ── 行 2：字体大小滑块（无极）──
    // ── 行 1.5：管理员权限（游戏等高完整性窗口内也能记录）──
    const float rowAdmin = y + Px(64.0f);
    ui.text("set.admin.label")
        .x(x + Px(24.0f)).y(rowAdmin).size(w - Px(230.0f), Px(30.0f))
        .text("管理员模式（游戏内也可记录，重启程序生效）")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    ui.stack("set.admin.row")
        .x(x + w - Px(160.0f)).y(rowAdmin - Px(4.0f)).size(Px(136.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.admin")
                .size(Px(136.0f), Px(38.0f))
                .checked(AdminModeFlagged())
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) {
                    if (!SetAdminModeFlagged(v)) {
                        s_recMsg = "设置失败（无法写入系统兼容性标记）";
                    } else if (v) {
                        s_recMsg = "将以管理员权限重启…";
                        s_recMsgAt = GetTickCount64() / 1000.0;
                        app::requestUpdate();
                        if (RelaunchAsAdmin()) {
                            Sleep(600);          // 等新提权实例接管
                            ExitAppNow();
                            return;
                        }
                        SetAdminModeFlagged(false);   // 用户取消 UAC → 回滚
                        s_recMsg = "已取消管理员授权，保持当前权限";
                    } else {
                        s_recMsg = "已关闭管理员模式，重启程序后生效";
                    }
                    s_recMsgAt = GetTickCount64() / 1000.0;
                    app::requestUpdate();
                })
                .build();
        })
        .build();

    // ── 行 2：开机自启动（只拉起记录程序 + 托盘图标，不带图形界面）──
    const float rowAuto = y + Px(124.0f);
    ui.text("set.autostart.label")
        .x(x + Px(24.0f)).y(rowAuto).size(w - Px(230.0f), Px(30.0f))
        .text("开机自启动（后台记录 + 托盘）")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    ui.stack("set.autostart.row")
        .x(x + w - Px(160.0f)).y(rowAuto - Px(4.0f)).size(Px(136.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.autostart")
                .size(Px(136.0f), Px(38.0f))
                .checked(AutostartEnabled())
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) {
                    const bool ok = AutostartSet(v);
                    s_recMsg = ok ? (v ? "已开启开机自启动（只启动记录程序）" : "已关闭开机自启动")
                                  : "设置失败（注册表写入被拒绝，请检查权限）";
                    s_recMsgAt = GetTickCount64() / 1000.0;
                    app::requestUpdate();
                })
                .build();
        })
        .build();

    const float row2 = y + Px(184.0f);
    const float sliderW = w - Px(48.0f) - Px(110.0f);
    const float shown = g_fontAuto ? AutoScaleForWidth(screenWidth) : g_fontCustom;
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

    // ── 自动开关行（字体大小行下方）：开关 + 说明合一 ──
    const float rowAutoFont = y + Px(268.0f);
    ui.stack("set.auto.row")
        .x(x + Px(24.0f)).y(rowAutoFont - Px(4.0f)).size(Px(120.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.auto")
                .size(Px(120.0f), Px(38.0f))
                .checked(g_fontAuto)
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) { SetFontAuto(v); app::requestUpdate(); })
                .build();
        })
        .build();
    ui.text("set.auto.label")
        .x(x + Px(158.0f)).y(rowAutoFont - Px(2.0f)).size(w - Px(200.0f), Px(42.0f))
        .text("自动：字号随窗口宽度缩放（宽窗口更大、窄窗口更小）。\n"
              "关闭后可拖动上方滑块统一调整界面全部字体。")
        .fontSize(m.typography.caption)
        .lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();

    // ── 行 4：说明文档（默认浏览器打开 GitHub 仓库的 README 页）──
    const float rowDoc = y + h - Px(52.0f);
    ui.text("set.doc.label")
        .x(x + Px(24.0f)).y(rowDoc + Px(4.0f)).size(w - Px(48.0f) - Px(160.0f), Px(30.0f))
        .text("说明文档：完整操作手册（在线页面）")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    MiniButton(ui, "set.doc", x + w - Px(174.0f), rowDoc - Px(4.0f), Px(150.0f), Px(38.0f),
               "打开说明文档", false, [] {
                   ShellExecuteW(nullptr, L"open",
                                 L"https://github.com/weilantianhai/keyboardStats#readme",
                                 nullptr, nullptr, SW_SHOWNORMAL);
               });
    ui.text("set.doc.credit")
        .x(x + Px(24.0f)).y(rowDoc + Px(38.0f)).size(w - Px(48.0f), Px(20.0f))
        .text("本程序界面基于开源框架 EUI-NEO (Apache-2.0, github.com/sudoevolve/EUI-NEO) 构建")
        .fontSize(m.typography.caption)
        .lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();

}

// 记录管理面板（含数据文件夹/数据文件两行）：同样画在滚动内容坐标系里
static void DrawRecordPanel(core::dsl::Ui& ui, float w, float y,
                            const components::theme::ThemeColorTokens& tk) {
    const auto& m = tk.metrics;

    const double nowSec = GetTickCount64() / 1000.0;
    if (nowSec - s_recInfoAt >= 3.0) {   // 缓存统计，避免每帧扫描文件
        s_recInfoAt = nowSec;
        s_recInfo = StorageDescribe();
    }

    const float bx = 0.0f, bw = w;
    const float by = y;
    const float bh = Px(kRecPanelH);
    ui.rect("rec.panel")
        .x(bx).y(by).size(bw, bh)
        .color(tk.surface)
        .radius(m.radius.section)
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
        .build();
    ui.text("rec.title")
        .x(bx + Px(24.0f)).y(by + Px(18.0f)).size(bw - Px(48.0f), Px(30.0f))
        .text("记录管理")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();

    // 统计行：条数 / 文件数 / 时间范围 / 占用
    std::string stats = "暂无记录";
    if (s_recInfo.events > 0) {
        const std::string first = Utf8(YmdToStr(s_recInfo.firstYmd));
        const std::string last = Utf8(YmdToStr(s_recInfo.lastYmd));
        char sizeBuf[32];
        if (s_recInfo.bytes >= 1024 * 1024)
            snprintf(sizeBuf, sizeof sizeBuf, "%.1f MB", s_recInfo.bytes / 1048576.0);
        else
            snprintf(sizeBuf, sizeof sizeBuf, "%.0f KB", s_recInfo.bytes / 1024.0);
        stats = WithCommas(s_recInfo.events) + " 条事件 · " + std::to_string(s_recInfo.files) +
                " 个数据文件 · " + first + " ~ " + last + " · " + sizeBuf;
    }
    ui.text("rec.stats")
        .x(bx + Px(24.0f)).y(by + Px(54.0f)).size(bw - Px(48.0f), Px(26.0f))
        .text(stats)
        .fontSize(m.typography.label)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();

    // ────────────────── 数据位置（文件夹 / 文件）──────────────────
    // 路径可能很长，中间省略以保证一行放得下
    auto elide = [](const std::string& s, size_t keep) {
        if (s.size() <= keep) return s;
        return s.substr(0, keep / 2 - 1) + "…" + s.substr(s.size() - (keep / 2 - 2));
    };

    const float miniH = Px(32.0f), miniGap = Px(8.0f);
    const float right = bx + bw - Px(24.0f);
    const float wSecond = Px(58.0f), wFirst = Px(72.0f);   // 数据文件夹行：主操作 + 复位
    const float valueW = right - (wFirst + wSecond + miniGap * 2) - (bx + Px(24.0f) + Px(88.0f));
    const float folderY = by + Px(86.0f);
    const float fileY = by + Px(126.0f);
    // 数据文件行有 3 个按钮（新建/选择/自动），宽度按同样比例分配
    const float fNew = Px(58.0f), fPick = Px(58.0f), fAuto = Px(58.0f);
    const float fileValueW = right - (fNew + fPick + fAuto + miniGap * 2) - (bx + Px(24.0f) + Px(88.0f));

    // 目标文件夹里没有数据文件时直接切换；有则记下来弹确认框
    auto requestFolderChange = [&](const std::wstring& dir) {
        const int n = (int)DataFilesInFolder(DataFolderPath()).size();
        if (n == 0) {
            std::wstring err;
            if (StorageSetDataFolder(dir, false, &err, nullptr)) {
                FetchStats();
                s_recInfoAt = 0.0;
                s_recMsg = "数据文件夹已切换到 " + Utf8(dir);
            } else {
                s_recMsg = "切换失败：" + Utf8(err);
            }
            s_recMsgAt = GetTickCount64() / 1000.0;
            app::requestUpdate();
            return;
        }
        s_pendingDir = dir;
        s_pendingDirFiles = n;
        app::requestUpdate();
    };

    auto pathRow = [&](const char* id, float rowY, const char* label, const std::string& value,
                       float textW) {
        ui.text(std::string(id) + ".label")
            .x(bx + Px(24.0f)).y(rowY + Px(6.0f)).size(Px(88.0f), Px(24.0f))
            .text(label)
            .fontSize(m.typography.label)
            .lineHeight(Px(24.0f))
            .color(g_theme.textMut)
            .build();
        ui.text(std::string(id) + ".value")
            .x(bx + Px(24.0f) + Px(88.0f)).y(rowY + Px(6.0f)).size(textW, Px(24.0f))
            .text(value)
            .fontSize(m.typography.body)
            .lineHeight(Px(24.0f))
            .color(g_theme.text)
            .build();
    };

    pathRow("rec.folder", folderY, "数据文件夹",
            elide(Utf8(DataFolderPath()), (size_t)std::max(12.0f, valueW / Px(7.0f))), valueW);
    MiniButton(ui, "rec.folder.pick", right - (wFirst + wSecond + miniGap), folderY,
               wFirst, miniH, "更改", false, [requestFolderChange] {
        std::wstring dir;
        if (!PickFolder(L"选择数据文件夹", &dir)) return;
        requestFolderChange(dir);
    });
    MiniButton(ui, "rec.folder.def", right - wSecond, folderY, wSecond, miniH, "默认", false,
               [requestFolderChange] {
        requestFolderChange(DefaultDataFolder());
    });

    const std::wstring curFile = DataFileName();
    pathRow("rec.file", fileY, "数据文件",
            elide(curFile.empty() ? std::string("自动（按月 events-YYYYMM.jsonl）") : Utf8(curFile),
                  (size_t)std::max(12.0f, fileValueW / Px(7.0f))), fileValueW);
    MiniButton(ui, "rec.file.new", right - (fNew + fPick + fAuto + miniGap * 2), fileY,
               fNew, miniH, "新建", false, [] {
        std::wstring name, err;
        if (StorageCreateDataFile(&name, &err)) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已新建并切换到 " + Utf8(name);
        } else {
            s_recMsg = "新建失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.file.pick", right - (fPick + fAuto + miniGap), fileY,
               fPick, miniH, "选择", false, [] {
        std::wstring path;
        if (!PickOpenFile(L"选择数据文件（JSONL 事件文件）",
                          L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0", &path,
                          DataFolderPath())) return;   // 从数据文件夹打开，别让用户自己找
        std::wstring err;
        const long n = StorageAdoptJsonl(path, &err);
        if (n >= 0) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已切换到 " + Utf8(DataFileName()) + "（" + WithCommas(n) + " 条事件）";
        } else {
            s_recMsg = "切换失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.file.auto", right - fAuto, fileY, fAuto, miniH, "自动", false, [] {
        std::wstring err;
        if (StorageSetDataFile(L"", &err)) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已恢复按月自动数据文件";
        } else {
            s_recMsg = "恢复失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });

    // 操作按钮行
    const int btnCount = 5;
    const float btnGap = Px(10.0f);
    const float btnW = (bw - Px(48.0f) - btnGap * (btnCount - 1)) / (float)btnCount;
    const float btnY = by + Px(176.0f);
    const float btnH = Px(44.0f);
    auto btnX = [&](int i) { return bx + Px(24.0f) + (float)i * (btnW + btnGap); };

    MiniButton(ui, "rec.import", btnX(0), btnY, btnW, btnH, "转入文件", false, [] {
        std::wstring path;
        if (!PickOpenFile(L"转入数据文件（会移动到数据文件夹并切换）",
                          L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0", &path,
                          DataFolderPath())) return;
        std::wstring err;
        const long n = StorageAdoptJsonl(path, &err);
        if (n >= 0) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已转入并切换，共 " + WithCommas(n) + " 条事件";
        } else {
            s_recMsg = "转入失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.expjsonl", btnX(1), btnY, btnW, btnH, "导出 JSONL", false, [] {
        std::wstring path;
        if (!PickSaveFile(L"导出记录（JSONL）", L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0",
                          L"jsonl", L"keyboardstats-export.jsonl", &path,
                          DataFolderPath())) return;
        long n = 0;
        s_recMsg = StorageExportJsonl(path, &n) ? ("已导出 " + WithCommas(n) + " 条事件")
                                                : "导出失败（无法写入文件）";
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.expcsv", btnX(2), btnY, btnW, btnH, "导出 CSV", false, [] {
        std::wstring path;
        if (!PickSaveFile(L"导出记录（CSV）", L"CSV 表格\0*.csv\0所有文件\0*.*\0\0",
                          L"csv", L"keyboardstats-export.csv", &path,
                          DataFolderPath())) return;
        long n = 0;
        s_recMsg = StorageExportCsv(path, &n) ? ("已导出 " + WithCommas(n) + " 条事件")
                                              : "导出失败（无法写入文件）";
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.opendir", btnX(3), btnY, btnW, btnH, "打开数据目录", false, [] {
        // 明确打开"当前数据文件夹"，并把路径回显到提示行：
        // 之前数据文件夹默认在 exe 同级，打开后看着像打开了程序目录，容易误判。
        const std::wstring dir = DataFolderPath();
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
            CreateDirectoryW(dir.c_str(), nullptr);   // 还没建出来就先建，别静默失败
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr,
                                          SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            s_recMsg = "打开失败：" + Utf8(dir);
        } else {
            s_recMsg = "已打开 " + Utf8(dir);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.clear", btnX(4), btnY, btnW, btnH, "清除全部记录", true, [] {
        s_clearConfirm = 1;   // 第一步确认
        app::requestUpdate();
    });

    // 操作结果提示（显示 6 秒）：固定在按钮行下方，不再用面板底边反推
    if (!s_recMsg.empty() && nowSec - s_recMsgAt < 6.0) {
        ui.text("rec.msg")
            .x(bx + Px(24.0f)).y(by + Px(228.0f)).size(bw - Px(48.0f), Px(28.0f))
            .text(s_recMsg)
            .fontSize(m.typography.label)
            .lineHeight(Px(26.0f))
            .color(g_theme.text)
            .build();
    }
}

// 设置页：内容整体放进滚动视图，窗口再矮也不会重叠或截断
void DrawSettingsPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;
    const float x = Px(28.0f), y = ContentTop();
    const float w = std::min(screen.width - Px(56.0f), Px(760.0f));
    const float viewH = std::max(Px(120.0f), screen.height - y - Px(24.0f));
    const float gapY = Px(kPanelGap);

    const float contentH = Px(kFontPanelH) + gapY + Px(kRecPanelH);

    const float off = ScrollArea(ui, "set.scroll", x, y, w, viewH, contentH, s_settingsScroll,
                                 [&](core::dsl::Ui& su, float cw) {
                                     DrawFontPanel(su, cw, 0.0f, tk, screen.width);
                                     DrawRecordPanel(su, cw, Px(kFontPanelH) + gapY, tk);
                                 });
    ScrollThumb(ui, "set.scroll", x, y, w, viewH, contentH, off);

    // 清除记录：两步确认弹窗
    if (s_clearConfirm > 0) {
        ui.rect("rec.mask")
            .size(screen.width, screen.height)
            .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
            .onClick([] { s_clearConfirm = 0; app::requestUpdate(); })
            .build();
        const float dw = Px(470.0f), dh = Px(210.0f);
        const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
        ui.rect("rec.dlg")
            .x(dx).y(dy).size(dw, dh)
            .color(tk.surface)
            .radius(Px(14.0f))
            .border(1.0f, g_theme.border)
            .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
            .build();
        ui.text("rec.dlg.title")
            .x(dx + Px(26.0f)).y(dy + Px(22.0f)).size(dw - Px(52.0f), Px(34.0f))
            .text(s_clearConfirm == 1 ? "确认清除全部记录？" : "再次确认：数据将永久丢失")
            .fontSize(m.typography.title)
            .lineHeight(m.typography.title + m.typography.lineGap)
            .color(g_theme.text)
            .build();
        const std::string dlgText = (s_clearConfirm == 1)
            ? ("将删除 " + WithCommas(s_recInfo.events) + " 条事件记录及计数缓存，"
               "此操作不可撤销。\n如需保留，请先使用“导出 JSONL”备份。")
            : "这是最后一次确认：点击“确认清除”后，所有记录立即删除且无法恢复。";
        ui.text("rec.dlg.text")
            .x(dx + Px(26.0f)).y(dy + Px(74.0f)).size(dw - Px(52.0f), Px(70.0f))
            .text(dlgText)
            .fontSize(m.typography.body)
            .lineHeight(Px(28.0f))
            .color(g_theme.textMut)
            .build();
        MiniButton(ui, "rec.dlg.cancel", dx + dw - Px(250.0f), dy + dh - Px(64.0f),
                   Px(104.0f), Px(44.0f), "取消", false,
                   [] { s_clearConfirm = 0; app::requestUpdate(); });
        MiniButton(ui, "rec.dlg.ok", dx + dw - Px(136.0f), dy + dh - Px(64.0f),
                   Px(110.0f), Px(44.0f),
                   s_clearConfirm == 1 ? "继续" : "确认清除", true, [] {
                       if (s_clearConfirm == 1) {
                           s_clearConfirm = 2;   // 第二步确认
                       } else {
                           StorageClearAll();
                           FetchStats();
                           s_recInfoAt = 0.0;
                           s_clearConfirm = 0;
                           s_recMsg = "已清除全部记录";
                           s_recMsgAt = GetTickCount64() / 1000.0;
                       }
                       app::requestUpdate();
                   });
    }

    // 切换数据文件夹：原文件夹里有数据文件时，问是否一起搬走
    if (!s_pendingDir.empty()) {
        ui.rect("dir.mask")
            .size(screen.width, screen.height)
            .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
            .onClick([] { s_pendingDir.clear(); app::requestUpdate(); })
            .build();
        const float dw = Px(520.0f), dh = Px(230.0f);
        const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
        ui.rect("dir.dlg")
            .x(dx).y(dy).size(dw, dh)
            .color(tk.surface)
            .radius(Px(14.0f))
            .border(1.0f, g_theme.border)
            .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
            .build();
        ui.text("dir.dlg.title")
            .x(dx + Px(26.0f)).y(dy + Px(22.0f)).size(dw - Px(52.0f), Px(34.0f))
            .text("切换数据文件夹")
            .fontSize(m.typography.title)
            .lineHeight(m.typography.title + m.typography.lineGap)
            .color(g_theme.text)
            .build();
        const std::string dirDlgText =
            "新文件夹：" + Utf8(s_pendingDir) + "\n当前数据文件夹里有 " +
            std::to_string(s_pendingDirFiles) + " 个数据文件。是否把它们一起移动过去？";
        ui.text("dir.dlg.text")
            .x(dx + Px(26.0f)).y(dy + Px(74.0f)).size(dw - Px(52.0f), Px(70.0f))
            .text(dirDlgText)
            .fontSize(m.typography.body)
            .lineHeight(Px(28.0f))
            .color(g_theme.textMut)
            .build();
        MiniButton(ui, "dir.dlg.cancel", dx + Px(26.0f), dy + dh - Px(64.0f),
                   Px(96.0f), Px(44.0f), "取消", false,
                   [] { s_pendingDir.clear(); app::requestUpdate(); });
        MiniButton(ui, "dir.dlg.keep", dx + dw - Px(300.0f), dy + dh - Px(64.0f),
                   Px(120.0f), Px(44.0f), "仅切换", false, [] {
                       const std::wstring dir = s_pendingDir;
                       s_pendingDir.clear();
                       std::wstring err;
                       if (StorageSetDataFolder(dir, false, &err, nullptr)) {
                           FetchStats();
                           s_recInfoAt = 0.0;
                           s_recMsg = "已切换（原数据留在原文件夹）";
                       } else {
                           s_recMsg = "切换失败：" + Utf8(err);
                       }
                       s_recMsgAt = GetTickCount64() / 1000.0;
                       app::requestUpdate();
                   });
        MiniButton(ui, "dir.dlg.move", dx + dw - Px(170.0f), dy + dh - Px(64.0f),
                   Px(144.0f), Px(44.0f), "一起移动", true, [] {
                       const std::wstring dir = s_pendingDir;
                       s_pendingDir.clear();
                       std::wstring err;
                       FolderSwitchResult r;
                       if (StorageSetDataFolder(dir, true, &err, &r)) {
                           FetchStats();
                           s_recInfoAt = 0.0;
                           s_recMsg = "已切换，移动 " + std::to_string(r.moved) + " 个数据文件";
                           if (r.failed > 0)
                               s_recMsg += "，" + std::to_string(r.failed) + " 个失败（可能被占用）";
                       } else {
                           s_recMsg = "切换失败：" + Utf8(err);
                       }
                       s_recMsgAt = GetTickCount64() / 1000.0;
                       app::requestUpdate();
                   });
    }
}

// ────────────────────────── 主题页 ──────────────────────────

namespace {

// 自定义配色 / 自定义热力的取色目标（非空 = 取色面板打开）
std::string s_pickerTarget;
// 自定义方案是否走深色底（浅色底会让强调色更难压住）
bool s_customDark = true;

// ── 自实现 HSL 取色浮层 ──
// 框架的 colorPicker 只提供预设色板（没有色环/连续取色）。这里用 gradient +
// mouseArea 自己实现：色相条（12 段水平渐变拼成整圈）+ 饱和度条 + 明度条，
// 三根都支持按住拖动，拖动时实时应用（取消会还原打开时的状态）。
static double s_pickH = 210.0, s_pickS = 0.85, s_pickL = 0.55;
static core::Color s_pickSnap{};            // 打开时的颜色（取消时还原）
static int  s_pickSnapPal = 0;              // 打开时的配色下标
static int  s_pickSnapHeat = 0;             // 打开时的热力下标
static bool s_pickSnapHeatCustom = false;

static void PickerApply() {
    const core::Color c = ColorFromHsl(s_pickH, s_pickS, s_pickL);
    if (s_pickerTarget == "heat") SetCustomHeatBase(c);
    else SetCustomAccent(c, s_customDark);
}

static void PickerCancel() {
    if (s_pickerTarget == "heat") {
        SetCustomHeatBase(s_pickSnap);
        if (!s_pickSnapHeatCustom) SetHeatPalette(s_pickSnapHeat);
    } else {
        SetCustomAccent(s_pickSnap, s_customDark);
        if (s_pickSnapPal != PaletteCount() - 1) SetPalette(s_pickSnapPal);
    }
    s_pickerTarget.clear();
    app::requestUpdate();
}

static void OpenPicker(const char* which) {
    s_pickerTarget = which;
    const core::Color c = (std::string(which) == "heat") ? CustomHeatBase() : CustomAccent();
    ColorToHsl(c, &s_pickH, &s_pickS, &s_pickL);
    s_pickSnap = c;
    s_pickSnapPal = CurrentPalette();
    s_pickSnapHeat = CurrentHeatPalette();
    s_pickSnapHeatCustom = HeatIsCustom();
    app::requestUpdate();
}

// 浮层本体（后画覆盖先画；子元素一律不设 zIndex——MiniButton 不带 zIndex，
// 单方面抬高层级会把按钮盖到遮罩后面，关窗弹窗踩过这个坑）
static void DrawColorPickerOverlay(core::dsl::Ui& ui, const eui::Screen& screen,
                                   const components::theme::ThemeColorTokens& tk) {
    const bool forHeat = s_pickerTarget == "heat";
    const auto& m = tk.metrics;

    ui.rect("pick.mask")
        .size(screen.width, screen.height)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
        .onClick(PickerCancel)
        .build();

    const float dw = Px(470.0f), dh = Px(430.0f);
    const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
    ui.rect("pick.panel")
        .x(dx).y(dy).size(dw, dh)
        .color(tk.surface)
        .radius(Px(14.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
        .build();

    const float pad = Px(26.0f);
    const float sx = dx + pad, sw = dw - pad * 2.0f;

    ui.text("pick.title")
        .x(sx).y(dy + Px(20.0f)).size(sw, Px(30.0f))
        .text(forHeat ? "选择热力主色" : "选择主题色")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();
    ui.text("pick.sub")
        .x(sx).y(dy + Px(52.0f)).size(sw, Px(22.0f))
        .text(forHeat ? "按主色的同色相、由浅到深着色（平方根分档）"
                      : "以这个主色推导整套界面配色")
        .fontSize(m.typography.caption)
        .lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();

    // 当前颜色预览
    const core::Color cur = ColorFromHsl(s_pickH, s_pickS, s_pickL);
    const float pvY = dy + Px(84.0f);
    ui.rect("pick.chip")
        .x(sx).y(pvY).size(Px(46.0f), Px(34.0f))
        .color(cur).radius(Px(8.0f)).border(1.0f, g_theme.border)
        .build();
    ui.text("pick.hex")
        .x(sx + Px(60.0f)).y(pvY + Px(6.0f)).size(Px(140.0f), Px(24.0f))
        .text(ColorToHex(cur))
        .fontSize(m.typography.body)
        .lineHeight(Px(24.0f))
        .color(g_theme.text)
        .build();

    // 三根色条。mouseArea 的局部坐标已按元素逻辑宽度换算，e.x∈[0,宽]。
    const float sh = Px(26.0f);
    const float hueY = dy + Px(140.0f);
    const float satY = hueY + sh + Px(38.0f);
    const float litY = satY + sh + Px(38.0f);

    auto stripLabel = [&](const char* id, float ly, const char* text) {
        ui.text(id)
            .x(sx).y(ly).size(sw, Px(18.0f))
            .text(text)
            .fontSize(m.typography.caption)
            .lineHeight(Px(18.0f))
            .color(g_theme.textMut)
            .build();
    };
    stripLabel("pick.hue.label", hueY - Px(22.0f), "色相");
    stripLabel("pick.sat.label", satY - Px(22.0f), "饱和度");
    stripLabel("pick.lit.label", litY - Px(22.0f), "明度");

    // 色相条：12 段水平渐变拼成 360°（每段 30°）
    const int kSegs = 12;
    const float segW = sw / kSegs;
    for (int i = 0; i < kSegs; ++i) {
        ui.rect("pick.hue.s" + std::to_string(i))
            .x(sx + segW * i).y(hueY).size(segW + 0.5f, sh)
            .gradient(ColorFromHsl(i * 30.0, 1.0, 0.5),
                      ColorFromHsl((i + 1) * 30.0, 1.0, 0.5),
                      core::GradientDirection::Horizontal)
            .build();
    }
    components::mouseArea(ui, "pick.hue.area")
        .x(sx).y(hueY).size(sw, sh)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.0f})
        .cursor(core::CursorShape::Hand)
        .onPress([sw](const components::MouseEvent& e) {
            s_pickH = 360.0 * std::clamp(e.x / sw, 0.0f, 1.0f);
            PickerApply();
        })
        .onDrag([sw](const components::MouseDragEvent& e) {
            s_pickH = 360.0 * std::clamp(e.x / sw, 0.0f, 1.0f);
            PickerApply();
        })
        .build();

    // 饱和度条：灰 → 主色（横向）
    ui.rect("pick.sat.grad")
        .x(sx).y(satY).size(sw, sh)
        .gradient(ColorFromHsl(s_pickH, 0.0, s_pickL),
                  ColorFromHsl(s_pickH, 1.0, s_pickL),
                  core::GradientDirection::Horizontal)
        .build();
    components::mouseArea(ui, "pick.sat.area")
        .x(sx).y(satY).size(sw, sh)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.0f})
        .cursor(core::CursorShape::Hand)
        .onPress([sw](const components::MouseEvent& e) {
            s_pickS = std::clamp(static_cast<double>(e.x) / sw, 0.0, 1.0);
            PickerApply();
        })
        .onDrag([sw](const components::MouseDragEvent& e) {
            s_pickS = std::clamp(static_cast<double>(e.x) / sw, 0.0, 1.0);
            PickerApply();
        })
        .build();

    // 明度条：上白下黑，中点是纯色（两段纵向渐变）
    const float halfH = sh * 0.5f;
    ui.rect("pick.lit.top")
        .x(sx).y(litY).size(sw, halfH)
        .gradient(ColorFromHsl(s_pickH, s_pickS * 0.55, 0.97),
                  ColorFromHsl(s_pickH, s_pickS, 0.50),
                  core::GradientDirection::Vertical)
        .build();
    ui.rect("pick.lit.bot")
        .x(sx).y(litY + halfH).size(sw, halfH)
        .gradient(ColorFromHsl(s_pickH, s_pickS, 0.50),
                  ColorFromHsl(s_pickH, s_pickS, 0.03),
                  core::GradientDirection::Vertical)
        .build();
    components::mouseArea(ui, "pick.lit.area")
        .x(sx).y(litY).size(sw, sh)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.0f})
        .cursor(core::CursorShape::Hand)
        .onPress([sh](const components::MouseEvent& e) {
            s_pickL = std::clamp(1.0 - static_cast<double>(e.y) / sh, 0.0, 1.0);
            PickerApply();
        })
        .onDrag([sh](const components::MouseDragEvent& e) {
            s_pickL = std::clamp(1.0 - static_cast<double>(e.y) / sh, 0.0, 1.0);
            PickerApply();
        })
        .build();

    // 三根色条的游标（白条 + 深描边，深浅底上都看得见）
    auto thumbV = [&](const std::string& id, float tx, float ty) {
        ui.rect(id)
            .x(tx - Px(3.0f)).y(ty - Px(4.0f)).size(Px(6.0f), sh + Px(8.0f))
            .color(core::Color{1.0f, 1.0f, 1.0f, 0.95f})
            .radius(Px(3.0f))
            .border(1.0f, core::Color{0.0f, 0.0f, 0.0f, 0.35f})
            .build();
    };
    thumbV("pick.hue.thumb", sx + (float)(s_pickH / 360.0) * sw, hueY);
    thumbV("pick.sat.thumb", sx + (float)s_pickS * sw, satY);
    thumbV("pick.lit.thumb", sx + Px(4.0f), litY + (float)(1.0 - s_pickL) * sh);

    // 底部按钮：完成（保留当前，拖动时已实时应用）/ 取消（还原打开时状态）
    const float bw = Px(132.0f), bh = Px(36.0f);
    const float by = dy + dh - Px(58.0f);
    MiniButton(ui, "pick.ok", dx + dw - pad - bw, by, bw, bh, "完成", true, [] {
        s_pickerTarget.clear();
        app::requestUpdate();
    });
    MiniButton(ui, "pick.cancel", dx + dw - pad - bw * 2.0f - Px(12.0f), by, bw, bh,
               "取消", false, PickerCancel);
}

// 带色块的方案按钮：左侧一个小色片，右侧文字
void SwatchButton(core::dsl::Ui& ui, const std::string& id, float x, float y, float w, float h,
                  const std::string& label, core::Color chipA, core::Color chipB,
                  bool selected, std::function<void()> onClick) {
    const float r = Px(10.0f);
    ui.rect(id + ".bg")
        .x(x).y(y).size(w, h)
        .states(g_theme.panel, g_theme.panelHi, g_theme.panelActive)
        .radius(r)
        .border(selected ? 2.0f : 1.0f, selected ? g_theme.selected : g_theme.border)
        .onClick(std::move(onClick))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();

    const float chip = std::min(Px(26.0f), h - Px(20.0f));
    ui.rect(id + ".chipA")
        .x(x + Px(12.0f)).y(y + (h - chip) * 0.5f).size(chip, chip)
        .color(chipA).radius(Px(6.0f))
        .border(1.0f, g_theme.border)
        .build();
    if (chipB.a > 0.0f) {
        ui.rect(id + ".chipB")
            .x(x + Px(12.0f) + chip * 0.55f).y(y + (h - chip) * 0.5f).size(chip * 0.7f, chip)
            .color(chipB).radius(Px(6.0f))
            .border(1.0f, g_theme.border)
            .build();
    }
    ui.text(id + ".t")
        .x(x + Px(12.0f) + chip * 1.5f).y(y).size(w - Px(24.0f) - chip * 1.5f, h)
        .text(label)
        .fontSize(Px(15.0f))
        .lineHeight(Px(20.0f))
        .color(g_theme.text)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

// 热力方案按钮：三段色阶直接铺成一个小渐变条
void HeatSwatchButton(core::dsl::Ui& ui, const std::string& id, float x, float y, float w, float h,
                      const std::string& label, core::Color lo, core::Color mid, core::Color hi,
                      bool selected, std::function<void()> onClick) {
    const float r = Px(10.0f);
    ui.rect(id + ".bg")
        .x(x).y(y).size(w, h)
        .states(g_theme.panel, g_theme.panelHi, g_theme.panelActive)
        .radius(r)
        .border(selected ? 2.0f : 1.0f, selected ? g_theme.selected : g_theme.border)
        .onClick(std::move(onClick))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();

    const float barW = w - Px(24.0f);
    const float barH = Px(12.0f);
    const float barY = y + Px(12.0f);
    const core::Color stops[3] = {lo, mid, hi};
    const int seg = 24;
    for (int i = 0; i < seg; ++i) {
        const double t0 = (double)i / seg, t1 = (double)(i + 1) / seg;
        const core::Color c = HeatRampColor(stops[0], stops[1], stops[2], (t0 + t1) * 0.5);
        ui.rect(id + ".seg" + std::to_string(i))
            .x(x + Px(12.0f) + barW * (float)t0).y(barY)
            .size(barW / seg + 1.0f, barH)
            .color(c)
            .radius(i == 0 || i == seg - 1 ? Px(5.0f) : 0.0f)
            .build();
    }
    ui.text(id + ".t")
        .x(x + Px(12.0f)).y(barY + barH + Px(4.0f)).size(barW, h - barH - Px(16.0f))
        .text(label)
        .fontSize(Px(13.0f))
        .lineHeight(Px(18.0f))
        .color(g_theme.textMut)
        .build();
}

} // namespace

void DrawThemePage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    // 调试入口：--pick=1/2 启动即打开取色浮层（只消费一次）
    if (g_debugPick != 0) {
        OpenPicker(g_debugPick == 2 ? "heat" : "accent");
        g_debugPick = 0;
    }

    const float x = Px(28.0f), y = ContentTop();
    const float w = std::min(screen.width - Px(56.0f), Px(760.0f));
    const float viewH = std::max(Px(120.0f), screen.height - y - Px(24.0f));
    const float gapY = Px(kPanelGap);

    const float palH  = Px(kPalettePanelH);
    const float heatH = Px(kHeatPanelH);
    const float custH = Px(kCustomPanelH);
    const float contentH = palH + gapY + heatH + gapY + custH;

    const float pad = Px(24.0f);
    const float cellGap = Px(10.0f);
    auto panelBg = [&](const char* id, float py, float ph) {
        ui.rect(id)
            .x(0.0f).y(py).size(w, ph)
            .color(tk.surface)
            .radius(m.radius.section)
            .border(1.0f, g_theme.border)
            .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
            .build();
    };
    auto heading = [&](const char* id, float py, const char* text, const char* sub) {
        ui.text(id)
            .x(pad).y(py + Px(14.0f)).size(w - pad * 2.0f, Px(26.0f))
            .text(text)
            .fontSize(m.typography.title)
            .lineHeight(m.typography.title + m.typography.lineGap)
            .color(g_theme.text)
            .build();
        if (sub != nullptr) {
            ui.text(std::string(id) + ".sub")
                .x(pad).y(py + Px(42.0f)).size(w - pad * 2.0f, Px(22.0f))
                .text(sub)
                .fontSize(m.typography.caption)
                .lineHeight(Px(20.0f))
                .color(g_theme.textMut)
                .build();
        }
    };

    // 取色浮层打开时不画底层滚动区（避免遮罩下面还能滚动/悬停），整页只留浮层
    if (s_pickerTarget.empty()) {
        const float off = ScrollArea(ui, "theme.scroll", x, y, w, viewH, contentH, s_themeScroll,
                                     [&](core::dsl::Ui& su, float cw) {
                // ── 1. 配色方案 ──
                panelBg("th.pal.panel", 0.0f, palH);
                heading("th.pal.title", 0.0f, "配色方案",
                        "整套界面配色。选中项会立即应用并记住。");

                const int cols = 4;
                const float cellW = (cw - pad * 2.0f - cellGap * (cols - 1)) / cols;
                const float cellH = Px(52.0f);
                const float gridTop = Px(74.0f);
                const int total = PaletteCount();
                for (int i = 0; i < total; ++i) {
                    const int cx = i % cols, cy = i / cols;
                    const float bx = pad + (cellW + cellGap) * cx;
                    const float by = gridTop + (cellH + cellGap) * cy;
                    core::Color chipA, chipB;
                    if (i == PaletteCount() - 1) {
                        chipA = CustomAccent();
                        chipB = core::Color{0, 0, 0, 0};   // 自定义：只画一个主色片
                    } else {
                        PalettePreview(i, &chipA, &chipB);
                    }
                    SwatchButton(su, "th.pal." + std::to_string(i), bx, by, cellW, cellH,
                                 Utf8(PaletteName(i)), chipA, chipB,
                                 CurrentPalette() == i,
                                 [i] { SetPalette(i); app::requestUpdate(); });
                }

                // ── 2. 热力渐变 ──
                const float heatY = palH + gapY;
                panelBg("th.heat.panel", heatY, heatH);
                heading("th.heat.title", heatY, "热力渐变",
                        "热力图键帽的着色序列，和配色方案独立。");
                const int hcols = 4;
                const float hw = (cw - pad * 2.0f - cellGap * (hcols - 1)) / hcols;
                const float hh = Px(58.0f);
                const float hgt = heatY + Px(74.0f);
                const int htotal = HeatPaletteCount();
                for (int i = 0; i < htotal; ++i) {
                    const int cx = i % hcols, cy = i / hcols;
                    core::Color lo, mid, hi;
                    HeatPreviewOf(i, &lo, &mid, &hi);
                    HeatSwatchButton(su, "th.heat." + std::to_string(i),
                                     pad + (hw + cellGap) * cx, hgt + (hh + cellGap) * cy,
                                     hw, hh, Utf8(HeatPaletteName(i)), lo, mid, hi,
                                     CurrentHeatPalette() == i,
                                     [i] { SetHeatPalette(i); app::requestUpdate(); });
                }

                // ── 3. 自定义 ──
                const float custY = heatY + heatH + gapY;
                panelBg("th.cust.panel", custY, custH);
                heading("th.cust.title", custY, "自定义",
                        "用一个主色推导整套配色；热力色取色环上的颜色，并启用平方根色阶。");

                const float row1 = custY + Px(74.0f);
                const float btnW = Px(150.0f), btnH = Px(40.0f);
                MiniButton(su, "th.cust.accent", pad, row1, btnW, btnH, "选择主题色", false, [] {
                    s_pickerTarget = "accent";
                    app::requestUpdate();
                });
                su.rect("th.cust.accentprev")
                    .x(pad + btnW + Px(12.0f)).y(row1 + Px(8.0f)).size(Px(24.0f), Px(24.0f))
                    .color(CustomAccent()).radius(Px(6.0f))
                    .border(1.0f, g_theme.border)
                    .build();
                su.text("th.cust.accentdesc")
                    .x(pad + btnW + Px(46.0f)).y(row1 + Px(8.0f)).size(cw * 0.5f, Px(24.0f))
                    .text("当前主题色 " + ColorToHex(CustomAccent()))
                    .fontSize(m.typography.label)
                    .lineHeight(Px(24.0f))
                    .color(g_theme.textMut)
                    .build();
                // 自定义方案的深浅底
                MiniButton(su, "th.cust.tone", pad + btnW + Px(220.0f), row1, Px(110.0f), btnH,
                           s_customDark ? "深色底" : "浅色底", false, [] {
                               s_customDark = !s_customDark;
                               SetCustomAccent(CustomAccent(), s_customDark);
                           });

                const float row2 = row1 + Px(52.0f);
                MiniButton(su, "th.cust.heat", pad, row2, btnW, btnH, "选择热力主色", false, [] {
                    OpenPicker("heat");
                });
                su.rect("th.cust.heatprev")
                    .x(pad + btnW + Px(12.0f)).y(row2 + Px(8.0f)).size(Px(24.0f), Px(24.0f))
                    .color(CustomHeatBase()).radius(Px(6.0f))
                    .border(1.0f, g_theme.border)
                    .build();
                su.text("th.cust.heatdesc")
                    .x(pad + btnW + Px(46.0f)).y(row2 + Px(8.0f)).size(cw * 0.6f, Px(24.0f))
                    .text(HeatIsCustom() ? "已启用自定义热力色（平方根色阶）"
                                         : "当前为内置热力方案，点左侧按钮切换到自定义")
                    .fontSize(m.typography.label)
                    .lineHeight(Px(24.0f))
                    .color(g_theme.textMut)
                    .build();
                                 });
        ScrollThumb(ui, "theme.scroll", x, y, w, viewH, contentH, off);
    }

    // 取色浮层（自实现 HSL：色相/饱和度/明度三根可拖色条）
    if (!s_pickerTarget.empty()) DrawColorPickerOverlay(ui, screen, tk);
}

// ────────────────────────── 关窗确认弹窗 ──────────────────────────

void DrawCloseDialog(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!g_closeDialogOpen) return;
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    // 注意：整块弹窗**不要**设 zIndex —— MiniButton 内部不带 zIndex，
    // 一旦遮罩层用了更大的 zIndex，按钮就会跑到遮罩后面（看不见也点不到）。
    // 和"清除记录"那套弹窗一样，靠"后画覆盖先画"就够了。
    ui.rect("close.mask")
        .size(screen.width, screen.height)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
        .onClick([] { g_closeDialogOpen = false; app::requestUpdate(); })
        .build();
    const float dw = Px(520.0f), dh = Px(280.0f);
    const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
    ui.rect("close.dlg")
        .x(dx).y(dy).size(dw, dh)
        .color(tk.surface)
        .radius(Px(14.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
        .build();
    ui.text("close.title")
        .x(dx + Px(26.0f)).y(dy + Px(20.0f)).size(dw - Px(52.0f), Px(32.0f))
        .text("关闭窗口")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();
    // 手动换行：这一行太长会超出弹窗宽度
    ui.text("close.text")
        .x(dx + Px(26.0f)).y(dy + Px(64.0f)).size(dw - Px(52.0f), Px(76.0f))
        .text("关闭窗口后，后台记录程序会继续运行（内存占用极小），\n"
              "点任务栏托盘图标的「打开 KeyboardStats」随时唤起主窗口。\n"
              "「退出程序」会连后台记录一起退出。")
        .fontSize(m.typography.body)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();

    // 位置需要固定宽度的容器承载（组件自身无定位方法）
    ui.stack("close.check.row")
        .x(dx + Px(26.0f)).y(dy + Px(152.0f)).size(Px(300.0f), Px(34.0f))
        .content([&] {
            components::checkbox(ui, "close.check")
                .size(Px(280.0f), Px(34.0f))
                .checked(g_closeDontAsk)
                .text("不再提示，记住我的选择")
                .fontSize(m.typography.label)
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) { g_closeDontAsk = v; app::requestUpdate(); })
                .build();
        })
        .build();

    MiniButton(ui, "close.totray", dx + Px(26.0f), dy + dh - Px(64.0f),
               Px(196.0f), Px(44.0f), "关闭窗口", true,
               [] { CloseDialogDecide(false); });
    MiniButton(ui, "close.exit", dx + dw - Px(158.0f), dy + dh - Px(64.0f),
               Px(132.0f), Px(44.0f), "退出程序", false,
               [] { CloseDialogDecide(true); });
}

// ────────────────────────── 首次启动：自启动引导弹窗 ──────────────────────────

void DrawOnboardDialog(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!g_onboardOpen) return;
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    ui.rect("ob.mask")
        .size(screen.width, screen.height)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.50f})
        .build();   // 引导弹窗不允许点遮罩关闭（必须明确选择），也不做任何点击处理

    const float dw = Px(500.0f), dh = Px(300.0f);
    const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
    ui.rect("ob.dlg")
        .x(dx).y(dy).size(dw, dh)
        .color(tk.surface)
        .radius(Px(14.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
        .build();
    ui.text("ob.title")
        .x(dx + Px(26.0f)).y(dy + Px(22.0f)).size(dw - Px(52.0f), Px(32.0f))
        .text("开启开机自启动？")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();
    ui.text("ob.text")
        .x(dx + Px(26.0f)).y(dy + Px(66.0f)).size(dw - Px(52.0f), Px(110.0f))
        .text("开启后，电脑开机时会自动在后台记录键鼠使用，\n"
              "无需手动打开程序，安心无忧。\n"
              "后台记录只占约 2 MB 内存，托盘图标随时可\n"
              "以打开主窗口查看统计。")
        .fontSize(m.typography.body)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();
    ui.text("ob.hint")
        .x(dx + Px(26.0f)).y(dy + Px(170.0f)).size(dw - Px(52.0f), Px(22.0f))
        .text("之后也可以随时在「设置」页里开启或关闭。")
        .fontSize(m.typography.caption)
        .lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .build();

    MiniButton(ui, "ob.enable", dx + Px(26.0f), dy + dh - Px(64.0f),
               Px(196.0f), Px(44.0f), "开启自启动", true, [] {
                   const bool ok = AutostartSet(true);
                   PrefSetValue(L"ui-general.txt", "onboarded", "1");
                   g_onboardOpen = false;
                   app::requestUpdate();
                   (void)ok;   // 失败时设置页的开关状态仍准确（Enabled 查注册表）
               });
    MiniButton(ui, "ob.skip", dx + dw - Px(150.0f), dy + dh - Px(64.0f),
               Px(124.0f), Px(44.0f), "暂不", false, [] {
                   PrefSetValue(L"ui-general.txt", "onboarded", "1");
                   g_onboardOpen = false;
                   app::requestUpdate();
               });
}

} // namespace app
