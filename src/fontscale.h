#pragma once
// 字体/控件缩放策略（从 UI 代码中独立出来）
//   自动：由窗口宽度推导（宽窗口放大、窄窗口回落）
//   自定义：由设置页滑块统一指定
// 生效值 g_uiScale 供排版（Px）与主题字号（AppTheme）共同使用。
#include <string>

namespace app {

extern float g_uiScale;      // 当前生效缩放
extern bool  g_fontAuto;     // true=自动模式
extern float g_fontCustom;   // 自定义模式下的缩放值

constexpr float kFontScaleMin = 0.90f;
constexpr float kFontScaleMax = 1.60f;
constexpr float kFontScaleStep = 0.05f;   // 量化步长：限制不同字号的种类数

// 自动模式：基准宽度 1180 → 1.25，窄窗口回落、宽窗口放大
float AutoScaleForWidth(float width);

// 每帧调用（须在所有绘制前）：自动模式按窗口宽推导，自定义模式直接用设定值
void UpdateUiScale(float width);

void SetFontAuto(bool value);

// 滑块入口：量化后立即生效（拖动跟手）；文件写入由 TickFontScale 防抖
void RequestFontCustom(float value);
void TickFontScale(double nowSec);  // 由定时器调用：数值稳定后落盘

void LoadFontPref();

} // namespace app
