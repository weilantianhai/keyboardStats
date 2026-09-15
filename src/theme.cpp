#include "theme.h"
#include "fontscale.h"
#include "ui_util.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace app {

UiTheme g_theme;
bool g_lightMode = false;

namespace {

// ────────────────────────── 颜色工具 ──────────────────────────

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// HSL → RGB（h: 0..360, s/l: 0..1）。自定义主题整套配色都靠它推导。
core::Color Hsl(double h, double s, double l, float a = 1.0f) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 360.0;
    s = Clamp01(s);
    l = Clamp01(l);
    auto hue2rgb = [](double p, double q, double t) {
        if (t < 0.0) t += 1.0;
        if (t > 1.0) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 1.0 / 2.0) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    double r, g, b;
    if (s <= 0.0) {
        r = g = b = l;
    } else {
        const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        const double p = 2.0 * l - q;
        r = hue2rgb(p, q, h + 1.0 / 3.0);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0 / 3.0);
    }
    return {float(r), float(g), float(b), a};
}

void RgbToHsl(const core::Color& c, double* h, double* s, double* l) {
    const double r = c.r, g = c.g, b = c.b;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    *l = (mx + mn) * 0.5;
    if (mx - mn < 1e-6) {
        *h = 0.0;
        *s = 0.0;
        return;
    }
    const double d = mx - mn;
    *s = *l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == r) *h = 60.0 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) *h = 60.0 * ((b - r) / d + 2.0);
    else *h = 60.0 * ((r - g) / d + 4.0);
    if (*h < 0.0) *h += 360.0;
}

// 用相对亮度选一个可读的前景色（简单版 WCAG：0.55 阈值）
core::Color ReadableOn(const core::Color& bg) {
    const double lum = 0.2126 * bg.r + 0.7152 * bg.g + 0.0722 * bg.b;
    return lum > 0.55 ? core::Color{0.09f, 0.10f, 0.13f, 1.0f}
                      : core::Color{0.98f, 0.99f, 1.0f, 1.0f};
}

// ────────────────────────── 内置配色方案 ──────────────────────────

struct PaletteDef {
    const char* id;         // 持久化标识
    const wchar_t* name;
    bool dark;
    unsigned bg, panel, panelHi, panelActive, text, textMut, border;
    unsigned brand, selected, accent, idleKey, idleEdge;
};

// 方案里不写热力三色：热力色由独立的"热力方案"决定，两件事分开选。
const PaletteDef kPalettes[] = {
    // 0 深色（原有 Dark Dashboard）
    {"dark", L"深色", true,
     0x0B0C10, 0x141822, 0x1B2130, 0x232B3E, 0xE5E9F0, 0x8A93A6, 0x272D3B,
     0x1E40AF, 0x2563EB, 0xD97706, 0x232A3A, 0x323A4E},
    // 1 浅色
    {"light", L"浅色", false,
     0xF2F4F8, 0xFFFFFF, 0xEEF2F8, 0xE3EAF4, 0x1F2430, 0x5B6472, 0xD8DEE9,
     0x1E40AF, 0x2563EB, 0xB45309, 0xE6EAF2, 0xC9D2E0},
    // 2 莫兰迪（低饱和灰调，Nature Distilled 思路）
    {"morandi", L"莫兰迪", false,
     0xE8E4DE, 0xF5F2ED, 0xEDE9E2, 0xDFD9D0, 0x4A4640, 0x86807A, 0xD5CFC6,
     0x8C9A93, 0x7C8C86, 0xB08968, 0xE3DED6, 0xCCC5BB},
    // 3 赛博朋克（Retro Futurism：霓虹蓝/品红 + 深紫黑）
    {"cyber", L"赛博朋克", true,
     0x0D0A1A, 0x161230, 0x1E1840, 0x271F52, 0xE8E6FF, 0x9A93C4, 0x332A63,
     0x0080FF, 0x00C2FF, 0xFF006E, 0x1C1740, 0x3A2F70},
    // 4 复古终端（琥珀单色 CRT）
    {"retro", L"复古终端", true,
     0x12100B, 0x1C1810, 0x262017, 0x332A1C, 0xEBD9A8, 0xA08E63, 0x3D3320,
     0xC08A2E, 0xE0A33C, 0x7FB069, 0x241E14, 0x453A24},
    // 5 森林
    {"forest", L"森林", true,
     0x0C1512, 0x12201B, 0x182B23, 0x1F372C, 0xE3F0E8, 0x8AA79A, 0x27402F,
     0x2E7D4F, 0x3FA96A, 0xC9A227, 0x1B2A22, 0x2C4536},
    // 6 樱花
    {"sakura", L"樱花", false,
     0xFAF2F4, 0xFFFFFF, 0xF7EAEE, 0xF0DCE3, 0x3A2A31, 0x7A6570, 0xEBD3DA,
     0xC2185B, 0xD94B7F, 0x6A7DB7, 0xF6E6EB, 0xE4CBD4},
    // 7 深海
    {"deepsea", L"深海", true,
     0x061119, 0x0B1A26, 0x102434, 0x163044, 0xDCEDF5, 0x7E9AAC, 0x1D3547,
     0x1B6B8C, 0x2E9BBF, 0xE0A458, 0x0F2130, 0x1B3547},
    // 8 日落
    {"sunset", L"日落", true,
     0x1A1113, 0x251A1C, 0x312326, 0x3E2D30, 0xF6E7E1, 0xB2958E, 0x46322F,
     0xB5482E, 0xE06A3F, 0xF0B457, 0x2C1F21, 0x48332F},
    // 9 北欧极简
    {"nordic", L"北欧极简", false,
     0xEDEFF2, 0xFFFFFF, 0xE7EAEF, 0xDCE1E8, 0x2B3038, 0x646C78, 0xD3D8E0,
     0x3E5C76, 0x4E7495, 0xB06A4A, 0xE4E8ED, 0xC8CFD8},
};

constexpr int kPaletteCount = (int)(sizeof(kPalettes) / sizeof(kPalettes[0]));
constexpr int kCustomPalette = kPaletteCount;   // 最后一格是"自定义"

// ────────────────────────── 内置热力方案 ──────────────────────────

struct HeatDef {
    const char* id;
    const wchar_t* name;
    unsigned lo, mid, hi;
};

const HeatDef kHeats[] = {
    {"classic", L"经典 蓝→黄→红", 0x313695, 0xF0DC78, 0xB2182B},
    {"magma",   L"岩浆 暗红→亮黄", 0x4A0C0C, 0xD1440E, 0xFFD84D},
    {"ice",     L"冰川 深蓝→白",   0x123A63, 0x4FA8D8, 0xEAF6FF},
    {"neon",    L"霓虹 紫→青",     0x5B21B6, 0xE0399B, 0x22D3EE},
    {"mono",    L"灰度 墨→白",     0x2A2A2A, 0x8A8A8A, 0xF5F5F5},
    {"spectrum",L"光谱 蓝→绿→红",  0x1D4ED8, 0x22C55E, 0xEF4444},
};

constexpr int kHeatCount = (int)(sizeof(kHeats) / sizeof(kHeats[0]));
constexpr int kCustomHeat = kHeatCount;

// ────────────────────────── 当前选择（内存态） ──────────────────────────

int  g_palette = 0;
int  g_heat = 0;
bool g_heatCustom = false;
bool g_heatInvert = false;   // 颜色频率反转
core::Color g_customAccent{0.22f, 0.45f, 0.85f, 1.0f};
bool g_customDark = true;
core::Color g_customHeatBase{0.15f, 0.55f, 0.85f, 1.0f};

std::wstring HexStr(const char* key, const wchar_t* file) {
    return Widen(PrefGetValue(file, key, ""));
}

std::string ToHex(core::Color c) {
    char buf[16];
    snprintf(buf, sizeof buf, "%02X%02X%02X",
             (unsigned)std::lround(Clamp01(c.r) * 255.0),
             (unsigned)std::lround(Clamp01(c.g) * 255.0),
             (unsigned)std::lround(Clamp01(c.b) * 255.0));
    return buf;
}

bool ParseHex(const std::string& s, core::Color* out) {
    if (s.size() != 6) return false;
    unsigned v = 0;
    for (char ch : s) {
        v <<= 4;
        if (ch >= '0' && ch <= '9') v |= (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v |= (unsigned)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v |= (unsigned)(ch - 'A' + 10);
        else return false;
    }
    *out = Hex(v);
    return true;
}

// ────────────────────────── 由一个主色推导整套配色 ──────────────────────────
// 思路：主色决定色相 H；背景/面板只在 H 上做低饱和的深浅分级，
// 强调色取 H+150° 互补位，保证在界面上跳得出来又不刺眼。

UiTheme DeriveTheme(const core::Color& accent, bool dark) {
    double h = 0, s = 0, l = 0;
    RgbToHsl(accent, &h, &s, &l);
    const double hs = std::clamp(s, 0.25, 0.95);      // 太低会灰成一团
    const double comp = h + 150.0;

    UiTheme t;
    if (dark) {
        t.bg          = Hsl(h, hs * 0.30, 0.055);
        t.panel       = Hsl(h, hs * 0.34, 0.095);
        t.panelHi     = Hsl(h, hs * 0.36, 0.135);
        t.panelActive = Hsl(h, hs * 0.38, 0.175);
        t.border      = Hsl(h, hs * 0.34, 0.215);
        t.text        = Hsl(h, hs * 0.16, 0.930);
        t.textMut     = Hsl(h, hs * 0.20, 0.620);
        t.brand       = accent;
        t.selected    = Hsl(h, hs, std::max(0.52, l));
        t.accent      = Hsl(comp, hs * 0.80, 0.580);
        t.idleKey     = Hsl(h, hs * 0.34, 0.150);
        t.idleEdge    = Hsl(h, hs * 0.34, 0.245);
    } else {
        t.bg          = Hsl(h, hs * 0.22, 0.955);
        t.panel       = Hsl(h, hs * 0.10, 0.995);
        t.panelHi     = Hsl(h, hs * 0.20, 0.945);
        t.panelActive = Hsl(h, hs * 0.26, 0.900);
        t.border      = Hsl(h, hs * 0.24, 0.845);
        t.text        = Hsl(h, hs * 0.30, 0.130);
        t.textMut     = Hsl(h, hs * 0.16, 0.420);
        t.brand       = accent;
        t.selected    = Hsl(h, hs, std::min(0.44, l));
        t.accent      = Hsl(comp, hs * 0.70, 0.400);
        t.idleKey     = Hsl(h, hs * 0.20, 0.915);
        t.idleEdge    = Hsl(h, hs * 0.24, 0.800);
    }
    return t;
}

// 自定义热力主色 → 三段色阶。
// 按用户要求：**同一个色相，由浅到深**——低频=主色的浅调，高频=主色的深调。
// （之前那版把高频段偏了 42° 色相，看着就不像"选中的那个颜色"了。）
void CustomHeatRamp(core::Color base, core::Color* lo, core::Color* mid, core::Color* hi) {
    double h = 0, s = 0, l = 0;
    RgbToHsl(base, &h, &s, &l);
    const double hs = std::clamp(s, 0.30, 1.0);
    const double hl = std::clamp(l, 0.30, 0.75);
    // 以主色自身的明度为中心，往两端展开，保证"主色"在色阶里有落点
    const double topL = std::clamp(hl + 0.30, 0.62, 0.90);   // 最浅
    const double botL = std::clamp(hl - 0.34, 0.16, 0.42);   // 最深
    *lo  = Hsl(h, hs * 0.30, topL);              // 低频：淡
    *mid = Hsl(h, hs * 0.70, (topL + botL) * 0.5);
    *hi  = Hsl(h, hs,        botL);              // 高频：浓
}

core::Color HeatStop(int index) {
    // 注意：SetCustomHeatBase 只置 g_heatCustom，g_heat 仍停在内置方案下标上，
    // 所以这里必须看 g_heatCustom（否则自定义热力色永远不生效——上一版就是栽在这）。
    if (g_heatCustom || g_heat == kCustomHeat) {
        core::Color lo, mid, hi;
        CustomHeatRamp(g_customHeatBase, &lo, &mid, &hi);
        return index == 0 ? lo : (index == 1 ? mid : hi);
    }
    const HeatDef& d = kHeats[std::clamp(g_heat, 0, kHeatCount - 1)];
    return Hex(index == 0 ? d.lo : (index == 1 ? d.mid : d.hi));
}

// 把组件令牌套到当前 g_theme 上（原来叫 AppTheme）
components::theme::ThemeColorTokens BuildComponentTheme() {
    auto t = g_lightMode ? components::theme::light() : components::theme::dark();
    t.background    = g_theme.bg;
    t.surface       = g_theme.panel;
    t.surfaceHover  = g_theme.panelHi;
    t.surfaceActive = g_theme.panelActive;
    t.text          = g_theme.text;
    t.border        = g_theme.border;
    t.primary       = g_theme.selected;
    // 整体字号放大后再按窗口宽度缩放；control/spacing 只轻微缩放，
    // 避免超过框架 barChart 内硬编码的绘图区偏移（plotY=70 / h-112）
    const float ts = g_uiScale;
    const float ls = std::min(g_uiScale, 1.15f);
    t.metrics.typography.caption  = (t.metrics.typography.caption  + 4.0f) * ts;
    t.metrics.typography.label    = (t.metrics.typography.label    + 4.0f) * ts;
    t.metrics.typography.body     = (t.metrics.typography.body     + 4.0f) * ts;
    t.metrics.typography.title    = (t.metrics.typography.title    + 4.0f) * ts;
    t.metrics.typography.display  = (t.metrics.typography.display  + 4.0f) * ts;
    t.metrics.typography.subtitle = (t.metrics.typography.subtitle + 4.0f) * ts;
    t.metrics.typography.input    = (t.metrics.typography.input    + 4.0f) * ts;
    t.metrics.typography.hint     = (t.metrics.typography.hint     + 4.0f) * ts;
    t.metrics.typography.control *= ls;
    t.metrics.typography.lineGap *= ls;
    t.metrics.control.compact    *= ls;
    t.metrics.control.menuItem   *= ls;
    t.metrics.control.indicator  *= ls;
    t.metrics.spacing.large      *= ls;
    t.metrics.spacing.section    *= ls;
    t.metrics.spacing.compact    *= ls;
    return t;
}

} // namespace

// ────────────────────────── 对外接口 ──────────────────────────

int PaletteCount() { return kPaletteCount + 1; }

const char* PaletteId(int index) {
    if (index == kCustomPalette) return "custom";
    return kPalettes[std::clamp(index, 0, kPaletteCount - 1)].id;
}

const wchar_t* PaletteName(int index) {
    if (index == kCustomPalette) return L"自定义";
    return kPalettes[std::clamp(index, 0, kPaletteCount - 1)].name;
}

int CurrentPalette() { return g_palette; }

void ApplyPalette(int index, bool persist);

void SetPalette(int index) { ApplyPalette(index, true); }

void ApplyPalette(int index, bool persist) {
    index = std::clamp(index, 0, kCustomPalette);
    g_palette = index;
    if (index == kCustomPalette) {
        g_theme = DeriveTheme(g_customAccent, g_customDark);
        g_lightMode = !g_customDark;
    } else {
        const PaletteDef& p = kPalettes[index];
        g_lightMode = !p.dark;
        g_theme.bg          = Hex(p.bg);
        g_theme.panel       = Hex(p.panel);
        g_theme.panelHi     = Hex(p.panelHi);
        g_theme.panelActive = Hex(p.panelActive);
        g_theme.text        = Hex(p.text);
        g_theme.textMut     = Hex(p.textMut);
        g_theme.border      = Hex(p.border);
        g_theme.brand       = Hex(p.brand);
        g_theme.selected    = Hex(p.selected);
        g_theme.accent      = Hex(p.accent);
        g_theme.idleKey     = Hex(p.idleKey);
        g_theme.idleEdge    = Hex(p.idleEdge);
    }
    // 热力三色独立于配色方案
    g_theme.heatLo  = HeatStop(0);
    g_theme.heatMid = HeatStop(1);
    g_theme.heatHi  = HeatStop(2);

    if (persist) {
        PrefSetValue(L"ui-theme.txt", "palette", PaletteId(index));
        PrefSetValue(L"ui-theme.txt", "mode", g_lightMode ? "light" : "dark");
        PrefSetValue(L"ui-theme.txt", "heat",
                     g_heatCustom ? "custom" : kHeats[g_heat].id);
        PrefSetValue(L"ui-theme.txt", "accent", ToHex(g_customAccent).c_str());
        PrefSetValue(L"ui-theme.txt", "accentdark", g_customDark ? "1" : "0");
        PrefSetValue(L"ui-theme.txt", "heatbase", ToHex(g_customHeatBase).c_str());
    }
}

void PalettePreview(int index, core::Color* bg, core::Color* accent) {
    if (index == kCustomPalette) {
        const UiTheme t = DeriveTheme(g_customAccent, g_customDark);
        *bg = t.bg;
        *accent = t.selected;
        return;
    }
    const PaletteDef& p = kPalettes[std::clamp(index, 0, kPaletteCount - 1)];
    *bg = Hex(p.panel);
    *accent = Hex(p.selected);
}

void HeatPreviewOf(int index, core::Color* lo, core::Color* mid, core::Color* hi) {
    if (index == kCustomHeat) {
        CustomHeatRamp(g_customHeatBase, lo, mid, hi);
        return;
    }
    const HeatDef& d = kHeats[std::clamp(index, 0, kHeatCount - 1)];
    *lo = Hex(d.lo);
    *mid = Hex(d.mid);
    *hi = Hex(d.hi);
}

std::string ColorToHex(core::Color c) { return ToHex(c); }

core::Color ColorFromHsl(double h, double s, double l, float a) { return Hsl(h, s, l, a); }

double HueOfColor(core::Color c) {
    double h = 0, s = 0, l = 0;
    RgbToHsl(c, &h, &s, &l);
    return h;
}

void ColorToHsl(core::Color c, double* h, double* s, double* l) { RgbToHsl(c, h, s, l); }

void SetCustomAccent(core::Color accent, bool dark) {
    g_customAccent = accent;
    g_customDark = dark;
    ApplyPalette(kCustomPalette, true);
    app::requestUpdate();
}

core::Color CustomAccent() { return g_customAccent; }
bool CustomIsDark() { return g_customDark; }

int HeatPaletteCount() { return kHeatCount + 1; }

const char* HeatPaletteId(int index) {
    if (index == kCustomHeat) return "custom";
    return kHeats[std::clamp(index, 0, kHeatCount - 1)].id;
}

const wchar_t* HeatPaletteName(int index) {
    if (index == kCustomHeat) return L"自定义（色环 + 平方根色阶）";
    return kHeats[std::clamp(index, 0, kHeatCount - 1)].name;
}

int CurrentHeatPalette() { return g_heatCustom ? kCustomHeat : g_heat; }

void SetHeatPalette(int index) {
    index = std::clamp(index, 0, kCustomHeat);
    g_heatCustom = (index == kCustomHeat);
    g_heat = g_heatCustom ? 0 : index;
    g_theme.heatLo  = HeatStop(0);
    g_theme.heatMid = HeatStop(1);
    g_theme.heatHi  = HeatStop(2);
    PrefSetValue(L"ui-theme.txt", "heat", g_heatCustom ? "custom" : kHeats[g_heat].id);
    PrefSetValue(L"ui-theme.txt", "heatbase", ToHex(g_customHeatBase).c_str());
    app::requestUpdate();
}

void SetCustomHeatBase(core::Color base) {
    g_customHeatBase = base;
    g_heatCustom = true;
    g_theme.heatLo  = HeatStop(0);
    g_theme.heatMid = HeatStop(1);
    g_theme.heatHi  = HeatStop(2);
    PrefSetValue(L"ui-theme.txt", "heat", "custom");
    PrefSetValue(L"ui-theme.txt", "heatbase", ToHex(g_customHeatBase).c_str());
    app::requestUpdate();
}

core::Color CustomHeatBase() { return g_customHeatBase; }
bool HeatIsCustom() { return g_heatCustom; }
bool HeatUsesSqrtScale() { return g_heatCustom; }

bool HeatInverted() { return g_heatInvert; }

void SetHeatInverted(bool inverted) {
    g_heatInvert = inverted;
    PrefSetValue(L"ui-theme.txt", "invert", inverted ? "1" : "0");
    app::requestUpdate();
}

core::Color HeatRampColor(const core::Color& lo, const core::Color& mid, const core::Color& hi,
                          double t) {
    if (t <= 0.0) return g_theme.idleKey;
    const core::Color stops[3] = {lo, mid, hi};
    const double at[3] = {0.0, 0.5, 1.0};
    for (int i = 0; i < 2; ++i) {
        if (t <= at[i + 1]) {
            const double k = (t - at[i]) / (at[i + 1] - at[i]);
            auto lerp = [k](float a, float b) { return float(a + (b - a) * k); };
            return {lerp(stops[i].r, stops[i + 1].r),
                    lerp(stops[i].g, stops[i + 1].g),
                    lerp(stops[i].b, stops[i + 1].b), 1.0f};
        }
    }
    return hi;
}

core::Color HeatColor(double t) {
    if (t <= 0.0) return g_theme.idleKey;   // 未按键始终是 idle 灰，不参与反转
    // 自定义热力方案用平方根色阶：低频段的差异被拉开，小基数也能看出层次
    if (HeatUsesSqrtScale()) t = std::sqrt(std::clamp(t, 0.0, 1.0));
    if (g_heatInvert) {
        t = 1.0 - std::clamp(t, 0.0, 1.0);
        // 满频键反转后正好落在色阶最低端（t=0），那是合法颜色而不是"未按键"，
        // 给个极小值绕开 HeatRampColor 的 idle 分支
        if (t <= 0.0) t = 0.0001;
    }
    return HeatRampColor(g_theme.heatLo, g_theme.heatMid, g_theme.heatHi, t);
}

components::theme::ThemeColorTokens CurrentTheme() { return BuildComponentTheme(); }

void ApplyTheme() { ApplyPalette(g_palette, false); }

void LoadThemePref() {
    // 自定义色 / 自定义热力色先读，ApplyPalette 依赖它们
    core::Color tmp;
    if (ParseHex(PrefGetValue(L"ui-theme.txt", "accent", ""), &tmp)) g_customAccent = tmp;
    if (ParseHex(PrefGetValue(L"ui-theme.txt", "heatbase", ""), &tmp)) g_customHeatBase = tmp;
    g_customDark = PrefGetValue(L"ui-theme.txt", "accentdark", "1") != "0";

    const std::string heat = PrefGetValue(L"ui-theme.txt", "heat", "classic");
    g_heatCustom = (heat == "custom");
    g_heat = 0;
    for (int i = 0; i < kHeatCount; ++i)
        if (heat == kHeats[i].id) { g_heat = i; g_heatCustom = false; break; }
    g_heatInvert = PrefGetValue(L"ui-theme.txt", "invert", "0") == "1";

    const std::string palette = PrefGetValue(L"ui-theme.txt", "palette", "");
    int index = -1;
    for (int i = 0; i < kPaletteCount; ++i)
        if (palette == kPalettes[i].id) { index = i; break; }
    if (palette == "custom") index = kCustomPalette;

    if (index < 0) {
        // 没有 palette 偏好：兼容老的 mode=light|dark（含更老的裸文本格式）
        std::string mode = PrefGetValue(L"ui-theme.txt", "mode", "");
        bool light;
        if (!mode.empty()) {
            light = mode == "light";
        } else if (FILE* f = _wfopen(PrefFilePath(L"ui-theme.txt").c_str(), L"rb")) {
            char buf[16] = {};
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            buf[n] = 0;
            light = strstr(buf, "light") != nullptr;
        } else {
            // 无保存偏好：跟随系统（AppsUseLightTheme：1=浅色）
            DWORD v = 0, size = sizeof(v);
            light = false;
            if (RegGetValueW(HKEY_CURRENT_USER,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                             L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size)
                == ERROR_SUCCESS) {
                light = v == 1;
            }
        }
        index = light ? 1 : 0;
    }
    ApplyPalette(index, false);
}

void ToggleTheme() {
    // 头部按钮用：在深色/浅色两个基础方案之间切
    SetPalette(CurrentPalette() == 1 ? 0 : 1);
    app::requestUpdate();
}

} // namespace app
