#pragma once
// 无界面记录进程（--record）：全局钩子 + 计数落盘 + 托盘图标。
// 托盘菜单「打开」启动/唤起 GUI 主窗口；「退出」优雅停止记录。
// GUI 与记录进程是同一个 exe，靠命令行参数区分（见 app.cpp 的入口分流）。

namespace app {

// 命令行带 --record 时调用：进入记录模式的消息循环，**不返回**（内部 ExitProcess）。
// 必须在框架窗口创建之前调用（app.cpp 的静态初始化器里）。
[[noreturn]] void RecorderRun();

// GUI 侧：确保记录进程在跑；不在则拉起一个。返回 false = 拉起失败（GUI 兜底自记录）。
bool EnsureRecorderRunning();

// 托盘「打开」/双击图标：唤起已运行的 GUI 主窗口，没有就启动一个。
void RecorderShowGui();

} // namespace app
