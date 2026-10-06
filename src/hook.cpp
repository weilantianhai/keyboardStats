#include "hook.h"
#include "gamepad.h"
#include "storage.h"
#include "layout.h"
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

static HHOOK s_hook = nullptr;
static HHOOK s_mouseHook = nullptr;

// ── 实时按键状态共享内存 ──
// 钩子所在进程（--record 记录进程，或兜底自记录的 GUI）写入；GUI 只读。
// 布局：uint32 tick（最近一次事件的 GetTickCount）+ kKeySlots 字节状态（非 0=按下），
// 按 KeyCode 直接索引——键鼠用 0x00-0xFF，手柄用 0x100 段（见 layout.h）。
// tick 供 GUI 做超时消隐（滚轮没有"抬起"事件，靠它自动消失；也防 UP 丢失卡键）。
namespace {
struct SharedState {
    unsigned long tick;
    unsigned char state[kKeySlots];
    // v3 追加：手柄模拟量（摇杆/扳机）。键鼠路径完全不碰这段。
    unsigned long padTick;          // 模拟量最近更新时间
    short lx, ly, rx, ry;           // 摇杆 -32768..32767
    unsigned char lt, rt;           // 扳机 0..255
};
// 共享内存有三个版本，升级后旧记录进程可能仍在运行，因此：
//   v3（本版本）：上面这个结构，映射名 …KeyState3
//   v2：只有 tick + 512 状态；v1：tick + 256 状态
// 写方只用 v3 名称；读方按 v3 → v2 → v1 依次回退，并按版本决定映射多少字节
// （映射区之外读取会踩到未映射内存，故必须按版本裁量）。
constexpr wchar_t kSharedMapName3[] = L"Local\\KeyboardStats.KeyState3";
constexpr wchar_t kSharedMapName2[] = L"Local\\KeyboardStats.KeyState2";
constexpr wchar_t kSharedMapNameV1[] = L"Local\\KeyboardStats.KeyState";
constexpr const wchar_t* kSharedMapName = kSharedMapName3;   // 写方（记录进程/GUI 兜底）使用

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
                // 全结构清零：不能只清 state[]。padTick 若留下未初始化的垃圾值，
                // GUI 的"2 秒未更新"判定会误判（垃圾值可能落在未来/过去任意位置），
                // 表现为摇杆/扳机面板时好时坏；lx/ly/... 残留垃圾还会让
                // "读到的全是 0" 这个判断失效。
                ZeroMemory(shared, sizeof(SharedState));
                shared->tick = GetTickCount();
            }
        }
        tried = true;   // 创建失败不再重试（本进程负责写时才需要）
    }
    return shared;
}

// 只读视图（GUI 侧）：记录进程可能晚于 GUI 启动，按秒级间隔重试打开
SharedState* g_view = nullptr;
HANDLE g_viewMap = nullptr;
int g_viewSlots = 0;                 // 当前视图的有效键码数量（v2/v3=kKeySlots，v1=256）
int g_viewVersion = 0;               // 当前视图的共享内存版本（1/2/3）
unsigned long g_viewLastFail = 0;
unsigned long g_viewUpgradeAt = 0;   // 上一次尝试"从旧版本升到最新版"的时刻

// 当前最新版本。写方只用这个版本（见 kSharedMapName），读方回退到旧版本后
// 仍会定期回头找它——逻辑见 SharedKeyState 的升级分支。
constexpr int kSharedVersionLatest = 3;
// 连上旧版本后，隔多久再试一次能不能升到最新版。取 2 秒：足够覆盖记录进程的
// 启动耗时（StorageInit + InstallHook + GamepadStart），又不会让面板长时间缺数据。
constexpr unsigned long kViewUpgradeMs = 2000;

// 各版本共享内存的字节数：**必须按版本裁量**，映射区之外读取会踩到未映射内存
SIZE_T ViewBytesFor(int version) {
    if (version >= 3) return sizeof(SharedState);
    return sizeof(unsigned long) + (SIZE_T)(version == 1 ? 256 : kKeySlots);
}

// 打开指定版本的只读视图。成功返回视图指针并把映射句柄写进 outMap；
// 失败返回 nullptr，且不残留任何句柄。
SharedState* TryOpenView(int version, HANDLE* outMap) {
    const wchar_t* name = version >= 3 ? kSharedMapName3
                        : (version == 2 ? kSharedMapName2 : kSharedMapNameV1);
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!map) return nullptr;
    SharedState* v = (SharedState*)MapViewOfFile(map, FILE_MAP_READ, 0, 0,
                                                 ViewBytesFor(version));
    if (!v) {
        CloseHandle(map);
        return nullptr;
    }
    *outMap = map;
    return v;
}

} // namespace

// ── 排障探针（见 hook.h 的说明）──
// 注意：必须定义在匿名 namespace **之外**——调用点在别的翻译单元
// （gamepad.cpp / app.cpp），匿名 namespace 内的符号只对本文件可见，链接会找不到。
// 每行都 fflush，进程被强杀也不会丢最后几行，故不需要在 atexit 里收尾。

void PadDiagLogVersion() {
    static FILE* f = nullptr;
    static bool opened = false;
    static int last = -1;
    const int ver = SharedPadVersion();
    if (ver == last) return;                 // 只在变化时记一行
    last = ver;
    if (!opened) {
        opened = true;
        // **早退前先把 opened 置位**：命令行里没有 --padiag 时也只判一次，
        // 之后本函数退化成一个 SharedPadVersion() 调用，常态开销可以忽略。
        if (!wcsstr(GetCommandLineW(), L"--padiag")) { f = nullptr; return; }
        wchar_t tmp[MAX_PATH] = {};
        if (!GetTempPathW(MAX_PATH, tmp)) return;
        std::wstring path = std::wstring(tmp) + L"pad-version-" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
        f = _wfopen(path.c_str(), L"w, ccs=UTF-8");
    }
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fwprintf(f, L"%02u:%02u:%02u.%03u  shared=v%d  %s\n",
             t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, ver,
             ver >= 3 ? L"含手柄模拟量"
                      : (ver == 0 ? L"尚未连上"
                                  : L"旧版本，无模拟量区 —— 摇杆/扳机恒为空"));
    fflush(f);
}

void PadDiagLogPublish(const SharedPadAnalog& v) {
    static FILE* f = []() -> FILE* {
        if (!wcsstr(GetCommandLineW(), L"--padiag")) return nullptr;
        wchar_t tmp[MAX_PATH] = {};
        if (!GetTempPathW(MAX_PATH, tmp)) return nullptr;
        std::wstring path = std::wstring(tmp) + L"pad-publish-" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
        return _wfopen(path.c_str(), L"w, ccs=UTF-8");
    }();
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fwprintf(f, L"%02u:%02u:%02u.%03u  lx=%+.3f ly=%+.3f rx=%+.3f ry=%+.3f LT=%.3f RT=%.3f\n",
             t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
             v.lx, v.ly, v.rx, v.ry, v.lt, v.rt);
    fflush(f);
}

// 写入实时按键状态（键鼠钩子与手柄采集共用；GUI 侧只读）
void SetKeyState(KeyCode vk, bool down) {
    if (vk >= kKeySlots) return;
    SharedState* s = Shared();
    if (!s) return;
    s->state[vk] = down ? 1 : 0;
    s->tick = GetTickCount();
}

const unsigned char* SharedKeyState() {
    const unsigned long now = GetTickCount();

    if (!g_view) {
        if (now - g_viewLastFail < 1000) return nullptr;    // 打不开就每秒重试，不空转
        g_viewLastFail = now;
        // 按版本从新到旧回退：v3 含手柄模拟量，v2 只有状态数组，v1 是最早的 256 项
        static const int kOpenOrder[] = {3, 2, 1};
        for (int version : kOpenOrder) {
            HANDLE map = nullptr;
            SharedState* v = TryOpenView(version, &map);
            if (!v) continue;
            g_view = v;
            g_viewMap = map;
            g_viewVersion = version;
            g_viewSlots = (version == 1) ? 256 : kKeySlots;
            break;
        }
        if (!g_view) return nullptr;
    } else if (g_viewVersion < kSharedVersionLatest &&
               now - g_viewUpgradeAt >= kViewUpgradeMs) {
        // ── 为什么连上之后还要重试升级（真实缺陷的修复点）──
        // GUI 与记录进程是**两个进程**：GUI 首次调用本函数时，记录进程可能还没走到
        // 创建 v3 映射那一步（它要先 StorageInit → InstallHook → GamepadStart）。
        // 此刻若机器上有旧版本（v2/v1）的映射残留，上面的回退循环就会先命中它，
        // 而旧版本**没有** padTick / lx / ly / lt 那段，SharedPadAnalogRead() 恒 false
        // → 摇杆内圈与两条行程柱永远不动。偏偏分数是从磁盘 jsonl 读的，照涨，
        //   于是现象极易被误判成"渲染坏了"，实则数据根本没到 GUI。
        // 旧实现在 g_view 非空后永不再试，等于把这个毫秒级竞态固化成了**永久降级**。
        //
        // 线程安全：GUI 侧所有的 SharedKeyState 调用点（WM_TIMER 回调里的 SharedKeyAlive
        // 与 DSL compose 里的面板绘制）都在主线程，这里换视图不会有人在读旧指针。
        g_viewUpgradeAt = now;
        HANDLE map = nullptr;
        SharedState* v = TryOpenView(kSharedVersionLatest, &map);
        if (v) {
            UnmapViewOfFile(g_view);
            CloseHandle(g_viewMap);
            g_view = v;
            g_viewMap = map;
            g_viewVersion = kSharedVersionLatest;
            g_viewSlots = kKeySlots;
        }
    }
    return g_view->state;
}

int SharedKeySlotCount() {
    return SharedKeyState() ? g_viewSlots : 0;
}

int SharedPadVersion() {
    // 0 = 还没连上任何共享内存；1/2/3 = 对接到的版本。
    // **排障首要指标**：真机上只要打出这个数就能分岔——
    // 是 3，说明共享内存链路通，该去查 XInput 侧；
    // 停在 1 或 2，说明 GUI 被降级了，摇杆数据根本没送到，查渲染是白费力气。
    return SharedKeyState() ? g_viewVersion : 0;
}

// ── 手柄模拟量（写方：手柄采集线程）──
void SetPadAnalog(const SharedPadAnalog& v) {
    SharedState* s = Shared();
    if (!s) return;
    const auto toShort = [](float f) {
        return (short)std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
    };
    const auto toByte = [](float f) {
        return (unsigned char)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f);
    };
    s->lx = toShort(v.lx); s->ly = toShort(v.ly);
    s->rx = toShort(v.rx); s->ry = toShort(v.ry);
    s->lt = toByte(v.lt);  s->rt = toByte(v.rt);
    s->padTick = GetTickCount();
}

// ── 手柄模拟量（读方：GUI）──
bool SharedPadAnalogRead(SharedPadAnalog* out) {
    if (!out) return false;
    if (!SharedKeyState()) return false;              // 触发一次打开尝试
    if (g_viewVersion < 3) return false;              // 旧版本共享内存没有模拟量区
    if (GetTickCount() - g_view->padTick > 2000) return false;   // 记录进程已退出/久未更新
    out->lx = (float)g_view->lx / 32767.0f;
    out->ly = (float)g_view->ly / 32767.0f;
    out->rx = (float)g_view->rx / 32767.0f;
    out->ry = (float)g_view->ry / 32767.0f;
    out->lt = (float)g_view->lt / 255.0f;
    out->rt = (float)g_view->rt / 255.0f;
    return true;
}

bool SharedKeyAlive() {
    // 供 GUI 判断"最近 1 秒内是否有键按下"（有才需要重绘动画）。
    // view 指向 SharedState 开头，state 前面正好是 tick。
    const unsigned char* st = SharedKeyState();
    if (!st) return false;
    const unsigned long tick = *reinterpret_cast<const volatile unsigned long*>(st - 4);
    return (GetTickCount() - tick) < 1000;
}

bool SharedPadAnalogAlive() {
    // 手柄模拟量是否在更新。判定条件与 SharedPadAnalogRead 的"过期"阈值一致（2 秒），
    // 否则会出现"读到的是有效值、却不认为有更新 → 不重绘"的裂缝。
    // 旧版本共享内存没有 padTick 字段，绝不能越界读——按版本挡掉。
    if (!SharedKeyState()) return false;
    if (g_viewVersion < 3) return false;
    return (GetTickCount() - g_view->padTick) < 2000;
}

// ── 调试旁路（见 hook.h 的说明）──
// 与 SharedKeyState 相同的两步走：先确保视图已打开，再按版本挡掉越界读。
// 与 SharedPadAnalogRead 的唯一差别是**不做 2 秒过期判定**——注入器/记录进程
// 的更新时间戳对界面不重要，重要的是"字段里的值是什么"。
bool SharedPadAnalogReadRaw(SharedPadAnalog* out) {
    if (!out) return false;
    if (!SharedKeyState()) return false;
    if (g_viewVersion < 3) return false;
    out->lx = (float)g_view->lx / 32767.0f;
    out->ly = (float)g_view->ly / 32767.0f;
    out->rx = (float)g_view->rx / 32767.0f;
    out->ry = (float)g_view->ry / 32767.0f;
    out->lt = (float)g_view->lt / 255.0f;
    out->rt = (float)g_view->rt / 255.0f;
    return true;
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
            if (!repeat) RecordKey((KeyCode)vk);
            SetKeyState((KeyCode)vk, true);
        } else if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
            SetKeyState((KeyCode)vk, false);
        }
    }
    return CallNextHookEx(s_hook, code, wp, lp);
}

// 低级鼠标钩子：按键与滚轮计入同一张 counts 表（伪键码见 layout.h）。
// 只统计"按下"，不统计抬起，避免双击计数翻倍。
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lp;
        KeyCode vk = 0;
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

// 安装/卸载全部输入采集：键盘 + 鼠标低级钩子，以及手柄轮询线程（XInput）。
// 三者生命周期一致——任何使用 InstallHook 的进程都不会漏掉手柄。
void InstallHook() {
    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    GamepadStart();
}

void RemoveHook() {
    GamepadStop();
    if (s_hook) { UnhookWindowsHookEx(s_hook); s_hook = nullptr; }
    if (s_mouseHook) { UnhookWindowsHookEx(s_mouseHook); s_mouseHook = nullptr; }
}
