#pragma once

// 安装/卸载全局低级钩子（键盘 + 鼠标）
void InstallHook();
void RemoveHook();

// 实时按键状态（GUI 按下动画用）：256 键码，非 0 = 按下中。
// 钩子所在进程写入共享内存；其它进程通过本接口只读访问。
// 没有钩子在跑（或共享内存不可用）时返回 nullptr。
const unsigned char* SharedKeyState();
// 最近 1 秒内是否有按键事件（GUI 用于决定是否需要重绘动画）
bool SharedKeyAlive();
