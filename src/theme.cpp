#include "theme.h"
#include "fontscale.h"
#include "ui_util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

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

std::wstring PrefFilePath(const wchar_t* name) {
    wchar_t custom[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"KEYBOARDSTATS_DIR", custom, MAX_PATH) > 0) {
        return std::wstring(custom) + L"\\" + name;
    }
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    return std::wstring(appdata) + L"\\KeyboardStats\\" + name;
}

std::string PrefGetValue(const wchar_t* file, const char* key, const char* fallback) {
    FILE* f = _wfopen(PrefFilePath(file).c_str(), L"rb");
    if (!f) return fallback;
    std::string all;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);

    const std::string k = std::string(key) + "=";
    size_t pos = all.find(k);
    if (pos == std::string::npos) return fallback;
    pos += k.size();
    size_t end = all.find_first_of("\r\n", pos);
    return all.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

void PrefSetValue(const wchar_t* file, const char* key, const char* value) {
    const std::wstring path = PrefFilePath(file);
    std::map<std::string, std::string> kv;
    std::vector<std::string> order;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        std::string all;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
        fclose(f);
        size_t p = 0;
        while (p < all.size()) {
            size_t e = all.find_first_of("\r\n", p);
            if (e == std::string::npos) e = all.size();
            std::string line = all.substr(p, e - p);
            p = e + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            if (!kv.count(k)) order.push_back(k);
            kv[k] = line.substr(eq + 1);
        }
    }
    if (!kv.count(key)) order.push_back(key);
    kv[key] = value;

    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    for (const std::string& k : order) fputs((k + "=" + kv[k] + "\n").c_str(), f);
    fclose(f);
}

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
    std::string mode = PrefGetValue(L"ui-theme.txt", "mode", "");
    if (!mode.empty()) {
        g_lightMode = mode == "light";
    } else if (FILE* f = _wfopen(PrefFilePath(L"ui-theme.txt").c_str(), L"rb")) {
        // 旧格式（文件内容为裸的 light/dark）：兼容读取
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
    PrefSetValue(L"ui-theme.txt", "mode", g_lightMode ? "light" : "dark");
    app::requestUpdate();
}

} // namespace app
