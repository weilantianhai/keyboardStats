#pragma once
// pages_*.cpp 之间共享的内部状态与小构件。
// 不属于对外接口（外部只认 pages.h），因此单独放这里，不进 pages.h。
#include "pages.h"
#include "fontscale.h"

#include <cstdio>
#include <functional>
#include <string>

namespace app {

// 布局辅助：所有固定尺寸乘以窗口缩放因子，字号与留白同步变化
inline float Px(float v) { return v * g_uiScale; }

inline std::string FormatScale(float v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2fx", v);
    return buf;
}

// 控制行 Y（副标题下方）与页面内容起始 Y（控制行下方）
inline float ControlsY() { return Px(58.0f) + Px(28.0f) + 6.0f; }
inline float ContentTop() { return ControlsY() + Px(34.0f) + Px(14.0f); }

// ── 跨页面共享的状态 ──
extern bool s_top10Open;                        // 按键计数列表面板是否展开（头部按钮 + 热力图页）
extern eui::Signal<float> s_settingsScroll;     // 设置页滚动位置（跨帧保持）
extern eui::Signal<float> s_themeScroll;        // 主题页滚动位置（跨帧保持）

// 管理员开关的闪烁引导（头部提示跳转设置页时置位；设置页据此画脉冲高亮）
extern bool g_adminHighlight;
extern unsigned long long g_adminHighlightAt;
constexpr unsigned long long kAdminHighlightMs = 15000;   // 闪烁 15 秒后自动停止

// 设置页与主题页面板之间的统一间距（两边共用，必须同一份）
constexpr float kPanelGap = 12.0f;

// ── 通用小构件 ──
void MiniButton(core::dsl::Ui& ui, const std::string& id, float x, float y,
                float w, float h, const std::string& label, bool accent,
                std::function<void()> onClick);

// 简易滚动容器：内容高度由调用方给出，内容回调每帧只跑一次。
// 不用框架 ScrollView 的自动测量——它会把内容回调在独立测量 Ui 里再跑一遍，
// 那一遍创建的交互元素会留下按"内容坐标"算的命中框，操作几次后与真实元素抢悬停。
float ScrollArea(core::dsl::Ui& ui, const std::string& id, float x, float y, float w, float h,
                 float contentH, eui::Signal<float>& offSignal,
                 const std::function<void(core::dsl::Ui&, float)>& body);

// 自绘滚动条（框架内置的那条在本项目主题下不显示）
void ScrollThumb(core::dsl::Ui& ui, const char* id, float x, float y, float w, float viewH,
                 float contentH, float off);

} // namespace app
