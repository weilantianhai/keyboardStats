#include "hook.h"
#include "storage.h"
#include "layout.h"
#include <windows.h>

static HHOOK s_hook = nullptr;
static HHOOK s_mouseHook = nullptr;

// 低级键盘钩子：只观察（不拦截），按键时计数 +1、事件入缓冲。
// 注意：回调里绝不做磁盘 I/O（落盘由定时器统一处理）。
static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        DWORD vk = ((KBDLLHOOKSTRUCT*)lp)->vkCode;
        if (vk < 256) RecordKey((uint8_t)vk);
    }
    return CallNextHookEx(s_hook, code, wp, lp);
}

// 低级鼠标钩子：按键与滚轮计入同一张 counts 表（伪键码见 layout.h）。
// 只统计"按下"，不统计抬起，避免双击计数翻倍。
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lp;
        switch (wp) {
            case WM_LBUTTONDOWN: RecordKey(kMouseLeft); break;
            case WM_RBUTTONDOWN: RecordKey(kMouseRight); break;
            case WM_MBUTTONDOWN: RecordKey(kMouseMiddle); break;
            case WM_XBUTTONDOWN:
                RecordKey(HIWORD(ms->mouseData) == XBUTTON1 ? kMouseX1 : kMouseX2);
                break;
            case WM_MOUSEWHEEL:
                RecordKey(GET_WHEEL_DELTA_WPARAM(ms->mouseData) > 0 ? kWheelUp : kWheelDown);
                break;
            case WM_MOUSEHWHEEL:
                RecordKey(GET_WHEEL_DELTA_WPARAM(ms->mouseData) > 0 ? kWheelRight : kWheelLeft);
                break;
            default: break;
        }
    }
    return CallNextHookEx(s_mouseHook, code, wp, lp);
}

void InstallHook() {
    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
}

void RemoveHook() {
    if (s_hook) { UnhookWindowsHookEx(s_hook); s_hook = nullptr; }
    if (s_mouseHook) { UnhookWindowsHookEx(s_mouseHook); s_mouseHook = nullptr; }
}
