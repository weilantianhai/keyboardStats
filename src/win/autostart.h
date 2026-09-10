#pragma once
#include <string>

// 开机自启动（注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run）
std::wstring ExePath();
bool AutostartEnabled();
// 返回 false = 注册表打开/写入失败（权限等），调用方应提示用户
bool AutostartSet(bool enable);
