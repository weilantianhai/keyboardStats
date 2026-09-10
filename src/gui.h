#pragma once

// 创建主窗口、托盘图标，进入消息循环；startHidden=true 时不弹窗（自启动用）。
// 返回退出码。
int GuiRun(bool startHidden);
