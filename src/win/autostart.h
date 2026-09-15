#pragma once
#include <string>

// 开机自启动（注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run）
// 写入 " <exe> " --record；提权由「管理员模式」的 RUNASADMIN 兼容性标记承担。
std::wstring ExePath();
// 是否已开启。实现只读一次注册表（无子进程），可在渲染路径每帧调用；
// 值存在但指向别的 exe 路径时返回 false（开关状态保持诚实）。
bool AutostartEnabled();
// 开启 = 写键值后立即读回核验；关闭 = 删键值并清理早期版本的计划任务残留。
// 返回 false = 注册表打不开/写入被拒/读回对不上，调用方应提示用户。
bool AutostartSet(bool enable);
