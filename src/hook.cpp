#include "hook.h"
#include "storage.h"
#include <windows.h>

static HHOOK s_hook = nullptr;

// 低级键盘钩子：只观察（不拦截），按键时计数 +1、事件入缓冲。
// 注意：回调里绝不做磁盘 I/O（落盘由 1 秒定时器统一处理）。
static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        DWORD vk = ((KBDLLHOOKSTRUCT*)lp)->vkCode;
        if (vk < 256) RecordKey((uint8_t)vk);
    }
    return CallNextHookEx(s_hook, code, wp, lp);
}

void InstallHook() {
    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
}

void RemoveHook() {
    if (s_hook) { UnhookWindowsHookEx(s_hook); s_hook = nullptr; }
}
