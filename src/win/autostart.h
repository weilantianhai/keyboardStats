#pragma once
#include <string>

// 开机自启动（注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run）
std::wstring ExePath();
bool AutostartEnabled();
void AutostartSet(bool enable);
