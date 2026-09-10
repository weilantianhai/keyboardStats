#include "fontscale.h"
#include "theme.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

float g_uiScale = 1.0f;
bool  g_fontAuto = true;
float g_fontCustom = 1.25f;

// 待生效的自定义值（滑块拖动中）：稳定 400ms 后才真正应用
static float  s_pendingCustom = 1.25f;
static double s_pendingSince = 0.0;

// 量化到 0.05 步长：框架字形缓冲（图集）只有一页，字号种类越多越快写满，
// 因此自动模式与滑块都必须限制不同字号的个数。
static float Quantize(float v) {
    const float snapped = kFontScaleMin +
                          std::round((v - kFontScaleMin) / kFontScaleStep) * kFontScaleStep;
    return std::clamp(snapped, kFontScaleMin, kFontScaleMax);
}

float AutoScaleForWidth(float width) {
    return Quantize(1.25f * width / 1180.0f);
}

void UpdateUiScale(float width) {
    g_uiScale = g_fontAuto ? AutoScaleForWidth(width) : g_fontCustom;
}

void SetFontAuto(bool value) {
    g_fontAuto = value;
    PrefSetValue(L"ui-font.txt", "auto", value ? "1" : "0");
}

static void ApplyFontCustom(float value) {
    g_fontCustom = Quantize(value);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", g_fontCustom);
    PrefSetValue(L"ui-font.txt", "scale", buf);
}

void RequestFontCustom(float value) {
    const float q = Quantize(value);
    if (std::fabs(q - s_pendingCustom) < 0.001f) return;
    s_pendingCustom = q;
    s_pendingSince = 0.0;   // 0 = 需要重新计时
}

float PendingFontCustom() { return s_pendingCustom; }

void TickFontScale(double nowSec) {
    if (s_pendingCustom == g_fontCustom) return;
    if (s_pendingSince == 0.0) { s_pendingSince = nowSec; return; }
    if (nowSec - s_pendingSince >= 0.4) {   // 拖动停止后再应用，避免中间字号污染字形图集
        ApplyFontCustom(s_pendingCustom);
        s_pendingSince = 0.0;
    }
}

void LoadFontPref() {
    g_fontAuto = PrefGetValue(L"ui-font.txt", "auto", "1") != "0";
    g_fontCustom = Quantize((float)atof(PrefGetValue(L"ui-font.txt", "scale", "1.25").c_str()));
    s_pendingCustom = g_fontCustom;
    s_pendingSince = 0.0;
    UpdateUiScale(1180.0f);   // 首帧前的合理默认（随后每帧按真实宽度刷新）
}

} // namespace app
