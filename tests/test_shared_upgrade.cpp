// 回归测试：共享内存版本回退**必须能自愈**。
//
// 真实缺陷（2026-09-17 修复）：GUI 与记录进程是**两个进程**。GUI 首次读共享内存时，
// 记录进程可能还没来得及创建 v3 映射（它要先跑完 StorageInit → InstallHook →
// GamepadStart）。此刻若机器上有旧版本（v2/v1）的映射残留，读方的 v3→v2→v1 回退
// 就会先命中它。旧实现是 `if (!g_view)` 一次性打开、此后再不重试——等于把这个
// 毫秒级的竞态固化成**永久降级**：
//     SharedPadAnalogRead() 恒 false  → 摇杆内圈与两条行程柱永远不动；
//     而活跃分数走的是磁盘 jsonl，照涨不误；
//   ⇒ 现象极像"渲染坏了"，实则数据压根没送到 GUI，去查绘制纯属白费力气。
//
// 这里把"从旧版本自动升上来"钉死，防止以后又被改回一次性打开。
//
// 说明：用例会临时造一个 `Local\KeyboardStats.KeyState2` 来模拟"残留的旧进程"，
// 进程结束时句柄关闭、映射对象自动销毁，不会在系统里留下东西。
#include "../src/hook.h"
#include "../src/layout.h"

#include <windows.h>

#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) { ++g_pass; printf("  ok   %s\n", what); }
    else    { ++g_fail; printf("  FAIL %s\n", what); }
}

int main() {
    printf("=== 共享内存版本回退必须能自愈 ===\n");

    // ── ① 先只摆一个 v2（旧版本）映射：老布局 = tick(4) + state[512] ──
    // 这正是竞态窗口里会被先命中的那个对象：名字存在、但**没有** padTick/lx/ly 那段。
    constexpr wchar_t kMap2[] = L"Local\\KeyboardStats.KeyState2";
    const SIZE_T bytes2 = sizeof(unsigned long) + (SIZE_T)kKeySlots;
    HANDLE map2 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                    0, bytes2, kMap2);
    Check(map2 != nullptr, "造出 v2 旧版本映射（模拟残留的旧进程）");
    if (!map2) { printf("\nFAIL\n"); return 1; }
    void* view2 = MapViewOfFile(map2, FILE_MAP_ALL_ACCESS, 0, 0, bytes2);
    Check(view2 != nullptr, "v2 视图可映射（句柄保持到本用例结束）");
    if (!view2) { CloseHandle(map2); printf("\nFAIL\n"); return 1; }

    // ── ② GUI 来读：此刻还没有 v3，只能命中 v2 ──
    Check(SharedKeyState() != nullptr, "只有 v2 时仍能读到按键状态（不崩、不返回空）");
    Check(SharedPadVersion() == 2, "回退到 v2 —— 竞态窗口里确实会发生");
    Check(SharedKeySlotCount() == kKeySlots, "v2 下有效键码数仍按 512 计");
    SharedPadAnalog probe;
    Check(!SharedPadAnalogRead(&probe), "v2 下读不到模拟量（旧布局没有那段字段）");

    // ── ③ 记录进程姗姗来迟：把 v3 映射建起来并发布真实模拟量 ──
    SharedPadAnalog w;
    w.lx = 0.75f; w.ly = -0.5f; w.rx = -0.25f; w.ry = 0.4f; w.lt = 0.6f; w.rt = 0.2f;
    SetPadAnalog(w);   // 内部 Shared() 会创建 Local\KeyboardStats.KeyState3

    // ── ④ 等过升级重试间隔（kViewUpgradeMs = 2s）──
    Sleep(2300);

    // ── ⑤ 核心回归点：必须已经自动升上来 ──
    Check(SharedKeyState() != nullptr, "等待后连接仍正常");
    Check(SharedPadVersion() == 3,
          "★ 核心回归点：自动从 v2 升到 v3（旧实现会永久卡在 v2）");

    // 记录进程是**持续**发布的，这里也补一次刷新：SharedPadAnalogRead 有 2 秒过期判定，
    // 上面 Sleep 了 2.3 秒，不刷新的话 padTick 就"凉"了——那是过期逻辑在工作，不是升级失败。
    SetPadAnalog(w);
    SharedPadAnalog rd;
    if (SharedPadAnalogRead(&rd)) {
        Check(rd.lx > 0.74f && rd.lx < 0.76f, "升级后 lx 读到真值（0.75）");
        Check(rd.ly < -0.49f && rd.ly > -0.51f, "升级后 ly 读到真值且符号正确（-0.5）");
        Check(rd.rx < -0.24f && rd.rx > -0.26f, "升级后 rx 读到真值（-0.25）");
        Check(rd.ry > 0.39f && rd.ry < 0.41f, "升级后 ry 读到真值（0.4）");
        Check(rd.lt > 0.59f && rd.lt < 0.61f, "升级后 LT 读到真值（0.6）");
        Check(rd.rt > 0.19f && rd.rt < 0.21f, "升级后 RT 读到真值（0.2）");
    } else {
        Check(false, "升级后应能读到模拟量");
    }
    Check(SharedPadAnalogAlive(), "升级后判定为正在更新（面板才会跟着重绘）");

    // ── ⑥ 收尾：卸载视图、关句柄，保证不在系统里留下 KeyState2 ──
    UnmapViewOfFile(view2);
    CloseHandle(map2);
    Check(OpenFileMappingW(FILE_MAP_READ, FALSE, kMap2) == nullptr,
          "用例销毁句柄后 v2 映射已随之消失（没有污染系统）");

    printf("\n通过 %d，失败 %d\n", g_pass, g_fail);
    printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
