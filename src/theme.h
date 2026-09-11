#pragma once
// 主题令牌 + 多套配色方案 + 热力渐变方案 + 自定义主题色
// 设计令牌源自 ui-ux-pro-max-skill 生成结果（Data-Dense Dashboard），
// 配色方案参考其 styles.csv / colors.csv（Nature Distilled、Retro Futurism、Financial Dashboard 等）
#include "eui_neo.h"
#include "components/theme.h"
#include "pref.h"

#include <string>

namespace app {

struct UiTheme {
    core::Color bg;         // 页面背景
    core::Color panel;      // 卡片/面板
    core::Color panelHi;    // 卡片悬停
    core::Color panelActive;// 卡片按下
    core::Color text;       // 主文本
    core::Color textMut;    // 次文本
    core::Color border;
    core::Color brand;      // uiux Primary
    core::Color selected;   // 控件选中色
    core::Color accent;     // uiux Accent
    core::Color idleKey;    // 无按键键帽
    core::Color idleEdge;   // 无按键键帽描边
    core::Color heatLo;     // 热度下限
    core::Color heatMid;    // 热度中点
    core::Color heatHi;     // 热度上限
};

extern UiTheme g_theme;
extern bool g_lightMode;      // 当前方案是否为浅色系（决定组件令牌取 light/dark）

// ────────────────────────── 配色方案 ──────────────────────────

int          PaletteCount();                    // 含"自定义"在内
const char*  PaletteId(int index);              // 稳定标识（写进偏好，别用下标）
const wchar_t* PaletteName(int index);
int          CurrentPalette();
void         SetPalette(int index);             // 应用并持久化

// 用"一个主色 → 推导整套配色"的方式自定义（深浅由 baseDark 决定）
void       SetCustomAccent(core::Color accent, bool dark);
core::Color CustomAccent();
bool        CustomIsDark();

// ────────────────────────── 热力渐变方案 ──────────────────────────

int          HeatPaletteCount();
const char*  HeatPaletteId(int index);
const wchar_t* HeatPaletteName(int index);
int          CurrentHeatPalette();
void         SetHeatPalette(int index);

// 自定义热力主色：取色环上的一个颜色，配平方根色阶
void         SetCustomHeatBase(core::Color base);
core::Color  CustomHeatBase();
bool         HeatIsCustom();
bool         HeatUsesSqrtScale();               // 自定义方案用平方根色阶（低频段更易区分）
bool         HeatInverted();                    // 颜色频率反转：高频显低频色、低频显高频色
void         SetHeatInverted(bool inverted);    // 应用并持久化

// 依据当前全部偏好重算 g_theme + 组件令牌
void ApplyTheme();

// 组件主题（segmented/barChart/datePicker 等），随 g_lightMode 切换
components::theme::ThemeColorTokens CurrentTheme();

// 动效缓动（全 UI 统一 150-300ms 区间的短动效）
inline core::Transition Motion() {
    return core::Transition::make(0.18f, core::Ease::OutCubic);
}

// 热度渐变：0=未用（键帽灰），0..1 = 当前热力方案的三段色阶
core::Color HeatColor(double t);

// 旧接口保留：头部按钮/兼容用（等价于在深色/浅色之间切）
void LoadThemePref();
void ToggleTheme();

// 色阶辅助（供自定义/预览用）
core::Color HeatRampColor(const core::Color& lo, const core::Color& mid, const core::Color& hi, double t);

// 选择界面用的预览：不切换当前主题，只取某方案的取景色
void PalettePreview(int index, core::Color* bg, core::Color* accent);
void HeatPreviewOf(int index, core::Color* lo, core::Color* mid, core::Color* hi);

// #RRGGBB（大写）
std::string ColorToHex(core::Color c);

// HSL 互转（h: 0..360, s/l: 0..1）——自定义色条要用
core::Color ColorFromHsl(double h, double s, double l, float a = 1.0f);
double     HueOfColor(core::Color c);
void       ColorToHsl(core::Color c, double* h, double* s, double* l);

} // namespace app
