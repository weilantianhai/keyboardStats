#include "hook.h"
#include "storage.h"
#include "layout.h"
#include <windows.h>

static HHOOK s_hook = nullptr;
static HHOOK s_mouseHook = nullptr;

// ── 实时按键状态共享内存 ──
// 钩子所在进程（--record 记录进程，或兜底自记录的 GUI）写入；GUI 只读。
// 布局：uint32 tick（最近一次事件的 GetTickCount）+ 256 字节状态（非 0=按下）。
// tick 供 GUI 做超时消隐（滚轮没有"抬起"事件，靠它自动消失；也防 UP 丢失卡键）。
namespace {
struct SharedState {
    unsigned long tick;
    unsigned char state[256];
};
constexpr wchar_t kSharedMapName[] = L"Local\\KeyboardStats.KeyState";

SharedState* Shared() {
    static SharedState* shared = nullptr;
    static bool tried = false;
    if (!shared && !tried) {
        // 显式 NULL DACL（本机所有用户可访问）：程序以管理员运行时创建的共享内存
        // 必须能被同机其它进程读取（GUI 按下动画依赖它），默认 DACL 可能拒绝跨完整性访问
        SECURITY_ATTRIBUTES sa{};
        SECURITY_DESCRIPTOR sd{};
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                        0, sizeof(SharedState), kSharedMapName);
        if (map) {
            shared = (SharedState*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState));
            if (shared) {
                shared->tick = GetTickCount();
                ZeroMemory(shared->state, sizeof(shared->state));
            }
        }
        tried = true;   // 创建失败不再重试（本进程负责写时才需要）
    }
    return shared;
}

void SetKeyState(unsigned char vk, bool down) {
    SharedState* s = Shared();
    if (!s) return;
    s->state[vk] = down ? 1 : 0;
    s->tick = GetTickCount();
}
} // namespace

const unsigned char* SharedKeyState() {
    // 只读方（GUI）：记录进程可能晚于 GUI 启动，按秒级间隔重试打开
    static SharedState* view = nullptr;
    static HANDLE map = nullptr;
    static unsigned long lastFail = 0;
    if (!view) {
        const unsigned long now = GetTickCount();
        if (now - lastFail < 1000) return nullptr;
        lastFail = now;
        map = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedMapName);
        if (!map) return nullptr;
        view = (SharedState*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(SharedState));
        if (!view) { CloseHandle(map); map = nullptr; return nullptr; }
    }
    return view->state;
}

bool SharedKeyAlive() {
    // 供 GUI 判断"最近 1 秒内是否有键按下"（有才需要重绘动画）。
    // view 指向 SharedState 开头，state 前面正好是 tick。
    const unsigned char* st = SharedKeyState();
    if (!st) return false;
    const unsigned long tick = *reinterpret_cast<const volatile unsigned long*>(st - 4);
    return (GetTickCount() - tick) < 1000;
}

// 低级键盘钩子：只观察（不拦截），按键时计数 +1、事件入缓冲、写实时状态。
// 注意：回调里绝不做磁盘 I/O（落盘由定时器统一处理）。
static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const DWORD vk = ((KBDLLHOOKSTRUCT*)lp)->vkCode;
        if (vk >= 256) return CallNextHookEx(s_hook, code, wp, lp);
        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
            // 按住不放时 OS 会以 ~30ms 间隔反复发 KEYDOWN（自动重复）。
            // 该键已处于按下态且状态很新 → 这是一次重复，只算一次，不重复计数。
            // （3 秒兜底：万一 UP 丢失把状态卡在"按下"，之后仍能恢复计数。）
            SharedState* s = Shared();
            const bool repeat = s && s->state[vk] && (GetTickCount() - s->tick) < 3000;
            if (!repeat) RecordKey((uint8_t)vk);
            SetKeyState((uint8_t)vk, true);
        } else if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
            SetKeyState((uint8_t)vk, false);
        }
    }
    return CallNextHookEx(s_hook, code, wp, lp);
}

// 低级鼠标钩子：按键与滚轮计入同一张 counts 表（伪键码见 layout.h）。
// 只统计"按下"，不统计抬起，避免双击计数翻倍。
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lp;
        unsigned char vk = 0;
        bool up = false;
        switch (wp) {
            case WM_LBUTTONDOWN: vk = kMouseLeft; break;
            case WM_LBUTTONUP:   vk = kMouseLeft; up = true; break;
            case WM_RBUTTONDOWN: vk = kMouseRight; break;
            case WM_RBUTTONUP:   vk = kMouseRight; up = true; break;
            case WM_MBUTTONDOWN: vk = kMouseMiddle; break;
            case WM_MBUTTONUP:   vk = kMouseMiddle; up = true; break;
            case WM_XBUTTONDOWN:
                vk = HIWORD(ms->mouseData) == XBUTTON1 ? kMouseX1 : kMouseX2; break;
            case WM_XBUTTONUP:
                vk = HIWORD(ms->mouseData) == XBUTTON1 ? kMouseX1 : kMouseX2; up = true; break;
            case WM_MOUSEWHEEL:
                vk = GET_WHEEL_DELTA_WPARAM(ms->mouseData) > 0 ? kWheelUp : kWheelDown; break;
            case WM_MOUSEHWHEEL:
                vk = GET_WHEEL_DELTA_WPARAM(ms->mouseData) > 0 ? kWheelRight : kWheelLeft; break;
            default: break;
        }
        if (vk) {
            if (up) {
                SetKeyState(vk, false);
            } else {
                RecordKey(vk);
                SetKeyState(vk, true);   // 滚轮没有对应 UP，GUI 靠 tick 超时消隐
            }
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
