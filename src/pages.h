#pragma once
// 页面绘制：头部、控制行、热力图页、Top 10、直方图页
#include "eui_neo.h"
#include "hook.h"   // SharedPadAnalog（调试旁路的出参类型）

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
// 管理员开关闪烁引导是否进行中（app 的重绘驱动轮询用）
bool AdminHighlightActive();
// 调试：跳转设置页并触发管理员开关闪烁引导（--hladmin）
void DebugHighlightAdmin();
// 调试开关：启动参数带 --padseed（给手柄灌梯度假数据 + 固定模拟量），
// 用于手边没有手柄时核对面板渲染、分组归一化与摇杆/扳机显示
bool DebugPadSeed();

// 调试旁路：启动参数带 --padmirror=<文件> 时，摇杆/扳机不再从共享内存取，
// 改读该文件里的一行 "lx ly rx ry lt rt"。
//
// 为什么需要它：共享内存的写入句柄是**独占**的——真手柄在跑时写入方是记录进程，
// 而开发时为了让界面出图会另起注入器进程，两者抢同一个映射名，后者往往被挡在门外，
// 界面读到的仍是记录进程发布的全 0（没插真手柄就永远是 0），
// 于是"数值变了界面跟不跟着动"这件事根本测不出来。
// 旁路直接读文件，绕开独占问题。正常启动完全不受影响。
bool DebugPadMirror(SharedPadAnalog* out);

} // namespace app
