#pragma once
#include "layout.h"   // KeyCode：实时状态按键码空间（键鼠 + 手柄）索引

// 安装/卸载全部输入采集：键盘 + 鼠标低级钩子 + 手柄轮询（XInput）
void InstallHook();
void RemoveHook();

// 写入实时按键状态（键鼠钩子与手柄采集共用；GUI 侧通过 SharedKeyState 只读）
void SetKeyState(KeyCode vk, bool down);

// ── 手柄模拟量（摇杆位置 + 扳机行程）──
// 由手柄采集线程持续发布；键鼠钩子不涉及。摇杆 -1..1（右/下为正），扳机 0..1。
struct SharedPadAnalog {
    float lx = 0.0f, ly = 0.0f;   // 左摇杆位置
    float rx = 0.0f, ry = 0.0f;   // 右摇杆位置
    float lt = 0.0f, rt = 0.0f;   // LT / RT 扳机行程
};

// 发布手柄模拟量（仅手柄采集线程调用）
void SetPadAnalog(const SharedPadAnalog& v);

// 读取当前手柄模拟量。以下情况返回 false（调用方画"居中/空行程"即可）：
//   · 对接的共享内存是旧版本（v1/v2，没有模拟量区）；· 数据超过 2 秒未更新（记录进程已退出）。
bool SharedPadAnalogRead(SharedPadAnalog* out);

// 跳过"2 秒未更新"这道过期判定，只按版本把关直接把当前值读出来。
//
// 为什么需要它：**一台机器上只能有一个进程持有共享内存的写入句柄**。
// 真手柄在跑时，写入方是独立的记录进程（--record）；而开发时为了让界面出图，
// 会另外起一个注入器进程写模拟量——两者抢同一个映射名，后者往往被挡在门外，
// 界面读到的还是记录进程发布的全 0（真手柄又没插，于是永远是 0）。
// 这个"旁路读取"配合 `--padmirror=<文件>` 使用：注入器把自己的值写进文件，
// 界面直接读文件，绕开共享内存的独占问题，用来验证"数值变了界面会不会跟着重绘"。
//
// 只应在 --padmirror 生效时调用；正常路径一律走 SharedPadAnalogRead。
bool SharedPadAnalogReadRaw(SharedPadAnalog* out);

// 排障探针（只有加 --padiag 时才打印）：每 2 秒把"本进程正准备发布的模拟量"
// 打到 %TEMP%\pad-publish.log。
//
// 为什么要在**调用点**打日志：SetPadAnalog 是内联在采集线程里的写操作，
// 而"到底有没有轮到它执行"没有别的办法从外部观察——真手柄一插上，
// 共享内存里就一定有某个进程（GUI 兜底自记录 / 记录进程）在持续写值，
// 外面再起注入器一律被挡在门外，于是共享内存里看到的值永远分不清是谁写的。
// 在调用点记录，配合"读同一块内存"的 tools/pad-diag.exe，
// 就能判出是"没人发布"还是"发布了但值不对"。
void PadDiagLogPublish(const SharedPadAnalog& v);

// 实时按键状态（GUI 按下动画用）：按 KeyCode 索引，非 0 = 按下中。
// 钩子所在进程写入共享内存；其它进程通过本接口只读访问。
// 没有钩子在跑（或共享内存不可用）时返回 nullptr。
const unsigned char* SharedKeyState();
// 当前实时状态视图的有效键码数量：kKeySlots（含手柄）；
// 若对接的是旧版本记录进程（v1 共享内存）则为 256——此时手柄动画不可用，读取超过该值不安全。
int SharedKeySlotCount();

// 当前对接到的共享内存版本：0=尚未连上，1/2/3=版本号。仅用于排障。
// 手柄模拟量只有 v3 才有；停在 1/2 意味着 GUI 永远读不到摇杆/扳机。
int SharedPadVersion();

// 最近 1 秒内是否有按键事件（GUI 用于决定是否需要重绘动画）
bool SharedKeyAlive();

// 排障探针（--padiag）：把 GUI 当前对接到的共享内存版本记到 %TEMP%\pad-version-<pid>.log，
// 只在版本**发生变化**时追加一行，避免每 500ms 刷屏。
// 判读：停在 1 或 2 就是被降级了，此时摇杆数据根本没到 GUI。
void PadDiagLogVersion();

// 手柄模拟量最近是否在更新（2 秒内）。
// 单独提供的原因：摇杆/扳机**不会更新 `tick`**（只有键鼠事件会），
// 所以纯推摇杆时 SharedKeyAlive() 恒为 false，靠它才能驱动面板重绘。
// 对接旧版本共享内存（v1/v2，没有模拟量区）时返回 false。
bool SharedPadAnalogAlive();
