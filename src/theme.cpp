#include "theme.h"
#include "ui_util.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace app {

UiTheme g_theme;
bool g_lightMode = false;

namespace {

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

} // namespace

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

components::theme::ThemeColorTokens CurrentTheme() { return AppTheme(); }

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

} // namespace app
