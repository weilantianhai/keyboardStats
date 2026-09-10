#pragma once
// UI 运行状态：GLFW 主线程单线程访问，页面/服务共享
#include "storage.h"
#include "eui/signal.h"

#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct TopEntry {
    std::string name;
    long count = 0;
    float frac = 0.0f;
    bool isMouse = false;   // 鼠标/滚轮伪键（用于分区显示与筛选）
};

extern int  g_page;           // 0=热力图 1=直方图
extern int  g_rangeMode;      // 0=今天 1=7天 2=30天 3=全部 4=自定义
extern uint32_t g_customFrom, g_customTo;    // 已应用的 yyyymmdd
extern uint32_t g_pendingFrom, g_pendingTo;  // 日期选择器中未应用的值

extern RangeStats g_stats;
extern long g_maxKey;
extern std::vector<float> g_barVals;
extern std::vector<std::string> g_barLabels;
extern std::vector<TopEntry> g_keyHist;   // 非零按键按次数升序（frac=次数/最大值）
extern std::string g_rangeText;

extern eui::Signal<bool> g_fromOpen;
extern eui::Signal<bool> g_toOpen;

// 按键计数筛选：0=键盘 1=鼠标 2=全部 3=分开（左键盘右鼠标）
extern int g_keyFilter;

// ── 关闭行为（点 × 时）──
// 0=每次询问 1=直接最小化到托盘 2=直接退出程序
constexpr int kCloseAsk    = 0;
constexpr int kCloseToTray = 1;
constexpr int kCloseExit   = 2;

extern bool g_closeDialogOpen;   // 弹窗是否显示
extern bool g_closeDontAsk;      // 弹窗里"不再提示"的勾选状态

int  CurrentCloseAction();
void SetCloseAction(int mode);      // 写入偏好
void CloseDialogDecide(bool exitApp);

// 立即退出（先落盘、卸钩子）
void ExitAppNow();

// 数据刷新（storage → 缓存），pages.cpp 的交互回调会调用
void FetchStats();

} // namespace app
