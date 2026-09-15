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

// 待落盘标记（滑块拖动中）：数值立即生效，文件写入等稳定后再做
static bool   s_persistPending = false;
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

static void PersistFontCustom() {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", g_fontCustom);
    PrefSetValue(L"ui-font.txt", "scale", buf);
}

// 滑块入口：量化后立即生效（拖动跟手）；落盘交由 TickFontScale 防抖
void RequestFontCustom(float value) {
    const float q = Quantize(value);
    if (std::fabs(q - g_fontCustom) < 0.001f) return;
    g_fontCustom = q;
    s_persistPending = true;
    s_pendingSince = 0.0;
}

void TickFontScale(double nowSec) {
    if (!s_persistPending) return;
    if (s_pendingSince == 0.0) { s_pendingSince = nowSec; return; }
    if (nowSec - s_pendingSince >= 0.4) {   // 拖动停止后写一次文件，避免频繁落盘
        PersistFontCustom();
        s_persistPending = false;
        s_pendingSince = 0.0;
    }
}

void LoadFontPref() {
    g_fontAuto = PrefGetValue(L"ui-font.txt", "auto", "1") != "0";
    g_fontCustom = Quantize((float)atof(PrefGetValue(L"ui-font.txt", "scale", "1.25").c_str()));
    s_persistPending = false;
    s_pendingSince = 0.0;
    UpdateUiScale(1180.0f);   // 首帧前的合理默认（随后每帧按真实宽度刷新）
}

} // namespace app
