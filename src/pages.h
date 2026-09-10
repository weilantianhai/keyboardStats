#pragma once
// 页面绘制：头部、控制行、热力图页、Top 10、直方图页
#include "eui_neo.h"

namespace app {

void DrawHeader(core::dsl::Ui& ui, float w);
void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawKeyHist(core::dsl::Ui& ui, const eui::Screen& screen);
void DrawHistPage(core::dsl::Ui& ui, const eui::Screen& screen);

} // namespace app
