#pragma once
// 页面绘制：头部、控制行、热力图页、Top 10、直方图页
#include "eui_neo.h"

namespace app {

void DrawHeader(core::dsl::Ui& ui, float w);
void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen);
// 主页看板：活跃分数 / 今日键盘 / 今日鼠标 / 滚轮格数 / 使用天数 / 活跃天数
void DrawBoard(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawKeyList(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawHistPage(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawSettingsPage(core::dsl::Ui& ui, const eui::Screen& screen);
// 按键使用次数直方图（升序），绘制在给定矩形内
void DrawKeyHist(core::dsl::Ui& ui, float x, float y, float w, float h);
// 主题页：配色方案 / 热力方案 / 自定义主色
void DrawThemePage(core::dsl::Ui& ui, const eui::Screen& screen);
// 关窗确认弹窗（跨页面显示在最上层）
void DrawCloseDialog(core::dsl::Ui& ui, const eui::Screen& screen);
// 首次启动的自启动引导弹窗（显示在最上层，盖过其它弹窗）
void DrawOnboardDialog(core::dsl::Ui& ui, const eui::Screen& screen);

} // namespace app
