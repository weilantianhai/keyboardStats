// Xbox 手柄采集实现：后台线程轮询 XInputGetState。
//
// 实现要点：
//   ① XInput 动态加载（LoadLibrary + GetProcAddress），不静态链接 xinput.lib——
//      静态链接会在启动时绑定 xinput1_4.dll，老系统上直接导致程序打不开；
//      动态加载失败时静默降级，键鼠照常记录。
//   ② 计数与实时状态分开处理：计数按**每个槽位**的上升沿（多手柄分别累计），
//      实时按下状态按**所有槽位的并集**（两个手柄按同一个键不会互相清掉对方）；
//   ③ 摇杆位置与扳机行程是模拟量，每轮把**第一个已连接手柄**的值发布到共享内存，
//      供热力图页的手柄面板实时显示（内圈位置 / 两条行程柱）。
//   ④ **行程累计（里程表模型）**：不保存轨迹，每个采样周期只算"这一小步走了多远"
//      并累加到总行程。摇杆是二维量，必须用**欧氏距离增量**——只比较半径（离圆心多远）
//      的话，绕圈推杆会算成"半径几乎没变 = 没动"，行程直接归零。
//      累计量按日聚合交给 storage（见 PadTravelAdd），本文件只管"增量怎么算"。
#include "gamepad.h"

#include "hook.h"
#include "layout.h"
#include "padpref.h"
#include "storage.h"

#include <windows.h>

#include <cmath>

namespace {

// XInputGetState 的数据结构改在 gamepad.h 声明（PollOnceWith 与单测都要看见）。

constexpr int   kMaxSlots   = 4;      // XInput 固定 4 个用户槽位
constexpr DWORD kPollIdleMs = 1000;   // 无手柄：1s 探测一次，插上后能较快发现
                                      // 有手柄时的间隔来自偏好档位（app::PadRatePollMs）

HANDLE            s_thread = nullptr;
volatile LONG     s_stop = 0;
HMODULE           s_lib = nullptr;
XiGetStateFn      s_getState = nullptr;

unsigned short s_prev[kMaxSlots] = {};   // 各槽位上一次的按键位掩码（计数用）
unsigned short s_prevCombined = 0;       // 上一次的并集（实时状态用）

// XInput 官方死区（未达阈值视为未推动/未按下，避免静止时抖动让面板乱跳）
constexpr short kDeadLeftStick  = 7849;
constexpr short kDeadRightStick = 8689;
constexpr unsigned char kTriggerThreshold = 30;

// ── 行程累计的噪声门限 ──
// 归一化量（0..1）上的一小步小于这个值就当作静止抖动，不计入行程。
// 手柄静止时摇杆会有 ±1~2 LSB 的抖动，归一化后约 0.0001 量级；
// 门限取 0.004 ≈ 原始值 130，既能滤掉抖动，又不会吃掉真实的轻微推动。
constexpr float kStickNoiseGate   = 0.004f;
constexpr float kTriggerNoiseGate = 0.004f;

// 摇杆上一次的归一化位置（每槽位独立，避免多手柄互相干扰）
float s_lastLx[kMaxSlots] = {}, s_lastLy[kMaxSlots] = {};
float s_lastRx[kMaxSlots] = {}, s_lastRy[kMaxSlots] = {};
float s_lastLt[kMaxSlots] = {}, s_lastRt[kMaxSlots] = {};
bool  s_lastValid[kMaxSlots] = {};       // 该槽位是否已有有效基准（断连后清掉）

// 本轮累计出的行程增量（PollOnce 填，PollProc 提交给 storage）
// 注意：存的是**路程长度**（每步位移的模长累加），不是位移矢量的分量和。
// 分量和会因为往返抵消——摇杆推出去再拉回来，位移和是 0 而路程是 2 倍行程。
double s_travelL = 0.0, s_travelR = 0.0;     // 左右摇杆的二维路程
double s_travelLT = 0.0, s_travelRT = 0.0;   // 扳机一维路程

// 摇杆：原始 -32768..32767 → -1..1，扣除死区后重新拉伸到满量程
float NormalizeStick(short raw, short deadzone) {
    const float f = (float)raw / 32767.0f;
    const float dz = (float)deadzone / 32767.0f;
    const float a = f < 0.0f ? -f : f;
    if (a <= dz) return 0.0f;
    const float scaled = (a - dz) / (1.0f - dz);
    return f < 0.0f ? -scaled : scaled;
}

// 扳机：0..255 → 0..1（同样扣死区）
float NormalizeTrigger(unsigned char raw) {
    const float f = (float)raw / 255.0f;
    const float dz = (float)kTriggerThreshold / 255.0f;
    return f <= dz ? 0.0f : (f - dz) / (1.0f - dz);
}

// 依次尝试常见版本：xinput1_4 由 Win8+ 提供，1_3 / 9_1_0 兼容更老的系统
bool LoadXInput() {
    const wchar_t* names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (const wchar_t* n : names) {
        HMODULE h = LoadLibraryW(n);
        if (!h) continue;
        auto fn = (XiGetStateFn)GetProcAddress(h, "XInputGetState");
        if (fn) {
            s_lib = h;
            s_getState = fn;
            return true;
        }
        FreeLibrary(h);
    }
    return false;
}

// 一轮轮询：返回有手柄的槽位数。
// getState 做成参数是为了可注入——单测要靠假手柄验证"摇杆值有没有真的发出去"。
// 名字带 Impl：真正对外（含单测）的入口 PollOnceWith 定义在下面、匿名 namespace 之外。
int PollOnceImpl(XiGetStateFn getState) {
    if (!getState) return 0;
    unsigned short combined = 0;
    int connected = 0;
    // 各槽位这一轮的结果。模拟量要取"第一个已连接手柄"，必须**按值**留下来：
    // 直接存 &st.Gamepad 会指向循环体内每轮重新构造的局部变量，循环一结束
    // 那块栈内存就被后面几轮的 `st = {}` 清零，读到的恒为全 0（详见 gamepad.h）。
    bool slotOk[kMaxSlots] = {};
    XiGamepad slotPad[kMaxSlots];

    for (int slot = 0; slot < kMaxSlots; ++slot) {
        XiState st = {};
        if (getState((unsigned long)slot, &st) != 0) {
            // 掉线（或从未连接）：清掉该槽位的记忆，避免拔出后残留按下态。
            // **同时清掉行程基准**——否则重新插上时，新位置与"掉线前的位置"之间
            // 会算出一大段假行程（手柄可能被挪动过，或者换了只手柄）。
            s_prev[slot] = 0;
            s_lastValid[slot] = false;
            continue;
        }
        ++connected;
        slotOk[slot] = true;
        slotPad[slot] = st.Gamepad;          // 拷贝，不是指针
        const unsigned short now = st.Gamepad.wButtons;
        combined = (unsigned short)(combined | now);

        // 计数：上升沿 +1（按住不放不会持续累加，与键盘的"自动重复只算一次"一致）
        const unsigned short risen = (unsigned short)(now & ~s_prev[slot]);
        if (risen) {
            for (int i = 0; i < kPadBitCount; ++i)
                if (risen & kPadBits[i].bit) RecordKey(kPadBits[i].vk);
        }
        s_prev[slot] = now;

        // ── 行程累计（里程表）──
        const float lx = NormalizeStick(st.Gamepad.sThumbLX, kDeadLeftStick);
        const float ly = NormalizeStick(st.Gamepad.sThumbLY, kDeadLeftStick);
        const float rx = NormalizeStick(st.Gamepad.sThumbRX, kDeadRightStick);
        const float ry = NormalizeStick(st.Gamepad.sThumbRY, kDeadRightStick);
        const float lt = NormalizeTrigger(st.Gamepad.bLeftTrigger);
        const float rt = NormalizeTrigger(st.Gamepad.bRightTrigger);

        if (s_lastValid[slot]) {
            // 摇杆：二维位移必须走欧氏距离。绕圈推杆时半径变化很小，
            // 用 |r2-r1| 会把整圈行程算成 0。
            const float dlx = lx - s_lastLx[slot], dly = ly - s_lastLy[slot];
            const float drx = rx - s_lastRx[slot], dry = ry - s_lastRy[slot];
            const float dL = std::sqrt(dlx * dlx + dly * dly);
            const float dR = std::sqrt(drx * drx + dry * dry);
            // 门限：滤掉静止抖动。注意门限加在**步长**上，不是加在坐标上——
            // 加在坐标上会让缓慢但真实的推动被"吸附"成 0。
            if (dL >= kStickNoiseGate) s_travelL += dL;
            if (dR >= kStickNoiseGate) s_travelR += dR;

            // 扳机：一维，直接取差值绝对值
            const float dlt = std::fabs(lt - s_lastLt[slot]);
            const float drt = std::fabs(rt - s_lastRt[slot]);
            if (dlt >= kTriggerNoiseGate) s_travelLT += dlt;
            if (drt >= kTriggerNoiseGate) s_travelRT += drt;
        }
        s_lastLx[slot] = lx; s_lastLy[slot] = ly;
        s_lastRx[slot] = rx; s_lastRy[slot] = ry;
        s_lastLt[slot] = lt; s_lastRt[slot] = rt;
        s_lastValid[slot] = true;
    }

    // 实时状态：只写"发生变化"的位（并集口径，多手柄互不干扰）
    const unsigned short changed = (unsigned short)(combined ^ s_prevCombined);
    if (changed) {
        for (int i = 0; i < kPadBitCount; ++i) {
            if (!(changed & kPadBits[i].bit)) continue;
            SetKeyState(kPadBits[i].vk, (combined & kPadBits[i].bit) != 0);
        }
        s_prevCombined = combined;
    }

    // 摇杆/扳机：每轮发布一次（无手柄时发布全 0 → 面板内圈回到中心、两条行程柱清空）
    SharedPadAnalog analog;
    XiGamepad firstPad;
    if (PickFirstConnected(kMaxSlots, slotOk, slotPad, &firstPad)) {
        analog.lx = NormalizeStick(firstPad.sThumbLX, kDeadLeftStick);
        analog.ly = NormalizeStick(firstPad.sThumbLY, kDeadLeftStick);
        analog.rx = NormalizeStick(firstPad.sThumbRX, kDeadRightStick);
        analog.ry = NormalizeStick(firstPad.sThumbRY, kDeadRightStick);
        analog.lt = NormalizeTrigger(firstPad.bLeftTrigger);
        analog.rt = NormalizeTrigger(firstPad.bRightTrigger);
    }
    // 排障探针：--padiag 时把准备发布的值打到 %TEMP%（见 hook.h）
    PadDiagLogPublish(analog);
    SetPadAnalog(analog);
    return connected;
}

DWORD WINAPI PollProc(LPVOID) {
    while (!s_stop) {
        const int connected = PollOnceWith(s_getState);   // 真机路径：用 XInput 加载到的那个函数
        if (connected > 0) {
            // 把手柄累计出的行程增量提交给 storage（按日聚合）。
            // s_travelL / s_travelR 已经是**路程长度**，直接提交。
            if (s_travelL != 0.0 || s_travelR != 0.0 ||
                s_travelLT != 0.0 || s_travelRT != 0.0) {
                PadTravelAdd((float)s_travelL, (float)s_travelR,
                             (float)s_travelLT, (float)s_travelRT);
                s_travelL = s_travelR = 0.0;
                s_travelLT = s_travelRT = 0.0;
            }
            Sleep(app::PadRatePollMs(app::PadRateGet()));
        } else {
            Sleep(kPollIdleMs);
        }
    }
    return 0;
}

} // namespace

// ── 以下两个定义在匿名 namespace **之外**：单测要链接它们，
//    写进匿名 namespace 就只有本翻译单元可见，链接期才报 undefined reference。

// 对外入口。真机传 XInput 加载到的 s_getState；单测传假函数，
// 就能在没有手柄的机器上验证"摇杆值有没有真的发到共享内存"这整条链路。
int PollOnceWith(XiGetStateFn getState) { return PollOnceImpl(getState); }

// 一轮采集里挑第一个已连接手柄的手柄数据（按值，绝不能返回指向局部变量的指针）
bool PickFirstConnected(int slotCount, const bool ok[], const XiGamepad pads[], XiGamepad* out) {
    for (int i = 0; i < slotCount; ++i) {
        if (!ok[i]) continue;
        if (out) *out = pads[i];     // 按值拷贝
        return true;
    }
    return false;
}

void GamepadStart() {
    if (s_thread) return;                     // 已在跑
    if (!s_getState && !LoadXInput()) return; // 无 XInput 运行库：静默降级
    InterlockedExchange(&s_stop, 0);
    s_thread = CreateThread(nullptr, 0, PollProc, nullptr, 0, nullptr);
    if (s_thread) {
        SetThreadPriority(s_thread, THREAD_PRIORITY_BELOW_NORMAL);   // 不与前台争资源
    }
}

void GamepadStop() {
    if (s_thread) {
        InterlockedExchange(&s_stop, 1);
        WaitForSingleObject(s_thread, 2000);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
    // 线程停了之后，把还没提交的行程增量落一次——否则最后不到一个采样周期的
    // 行程会随线程退出一起丢掉（量很小，但不该丢）。
    if (s_travelL != 0.0 || s_travelR != 0.0 || s_travelLT != 0.0 || s_travelRT != 0.0) {
        PadTravelAdd((float)s_travelL, (float)s_travelR, (float)s_travelLT, (float)s_travelRT);
        s_travelL = s_travelR = 0.0;
        s_travelLT = s_travelRT = 0.0;
    }
    // 退出前清掉手柄的实时按下态（否则 GUI 可能看到卡住的键面）
    for (int i = 0; i < kPadBitCount; ++i) SetKeyState(kPadBits[i].vk, false);
    ZeroMemory(s_prev, sizeof s_prev);
    ZeroMemory(s_lastValid, sizeof s_lastValid);
    s_prevCombined = 0;
    if (s_lib) {
        FreeLibrary(s_lib);
        s_lib = nullptr;
        s_getState = nullptr;
    }
}
