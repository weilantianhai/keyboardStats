#include "fontscale.h"
#include "theme.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace app {

float g_uiScale = 1.0f;
bool  g_fontAuto = true;
float g_fontCustom = 1.25f;

float AutoScaleForWidth(float width) {
    return std::clamp(1.25f * width / 1180.0f, 1.05f, kFontScaleMax);
}

void UpdateUiScale(float width) {
    g_uiScale = g_fontAuto ? AutoScaleForWidth(width) : g_fontCustom;
}

void SetFontAuto(bool value) {
    g_fontAuto = value;
    PrefSetValue(L"ui-font.txt", "auto", value ? "1" : "0");
}

void SetFontCustom(float value) {
    g_fontCustom = std::clamp(value, kFontScaleMin, kFontScaleMax);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", g_fontCustom);
    PrefSetValue(L"ui-font.txt", "scale", buf);
}

void LoadFontPref() {
    g_fontAuto = PrefGetValue(L"ui-font.txt", "auto", "1") != "0";
    g_fontCustom = std::clamp((float)atof(PrefGetValue(L"ui-font.txt", "scale", "1.25").c_str()),
                              kFontScaleMin, kFontScaleMax);
    UpdateUiScale(1180.0f);   // 首帧前的合理默认（随后每帧按真实宽度刷新）
}

} // namespace app
