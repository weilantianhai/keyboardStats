// Split out of pages.cpp by page (pure code motion, behavior unchanged).
#include "pages_common.h"
#include "theme.h"
#include "state.h"
#include "ui_util.h"
#include "fontscale.h"
#include "layout.h"
#include "heatnorm.h"
#include "components/components.h"
#include "timeutil.h"
#include "storage.h"
#include "pref.h"
#include "win/filedialog.h"
#include "win/autostart.h"
#include "win/adminmode.h"
#include "hook.h"
#include "shellapi.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace app {

namespace {

// 主题页三个面板的高度（同设置页：按内容固定，不跟窗口高度挂钩）
constexpr float kPalettePanelH = 258.0f;

constexpr float kHeatPanelH    = 196.0f;

constexpr float kCustomPanelH  = 196.0f;

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

} // namespace app
