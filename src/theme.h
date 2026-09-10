#pragma once
// 主题令牌（深/浅两套，运行时可切换）+ 热度渐变 + 偏好持久化
// 设计令牌源自 ui-ux-pro-max-skill 生成结果（Data-Dense Dashboard）
#include "eui_neo.h"
#include "components/theme.h"

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
    core::Color heatLo;     // 热度下限（蓝）
    core::Color heatMid;    // 热度中点（黄）
    core::Color heatHi;     // 热度上限（红）
};

extern UiTheme g_theme;
extern bool g_lightMode;

// 全局 UI 缩放：随窗口宽度变化（基准宽度 1180 → 1.25 倍字号，窄窗口回落）
extern float g_uiScale;
void UpdateUiScale(float width);

// 热度渐变：0=未用（键帽灰），0..1 = 蓝 → 黄 → 红
core::Color HeatColor(double t);

// 组件主题（segmented/barChart/datePicker 等），随 g_lightMode 切换
components::theme::ThemeColorTokens CurrentTheme();

// 动效缓动（全 UI 统一 150-300ms 区间的短动效）
inline core::Transition Motion() {
    return core::Transition::make(0.18f, core::Ease::OutCubic);
}

// 偏好持久化 + 系统默认跟随；ToggleTheme 供头部按钮调用
void LoadThemePref();
void ToggleTheme();

} // namespace app
