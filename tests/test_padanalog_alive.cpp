// 回归测试：共享内存的"有没有在更新"判定。
//
// 背景（真实缺陷）：摇杆/扳机是**模拟量**，它们更新共享内存时只写 padTick，
// 不写 tick（tick 专给键鼠事件用）。GUI 的重绘定时器原本只问 SharedKeyAlive()
// （只看 tick），于是"纯推摇杆"时永远不重绘 —— 面板内圈和两条行程柱冻住，
// 同时看板分数也不刷新。修复后加了 SharedPadAnalogAlive()，这里把两根时间线
// 的关键行为钉死，避免以后又被改回去。
//
// 本测试直接链接 src/hook.cpp，用真实的 CreateFileMapping/OpenFileMapping 走一遍，
// 不做任何桩替换——因为要验的正是"读写两侧对协议的理解是否一致"。
#include "../src/hook.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) { ++g_pass; printf("  ok   %s\n", what); }
    else    { ++g_fail; printf("  FAIL %s\n", what); }
}

int main() {
    printf("=== 手柄模拟量实时性判定 ===\n");

    // ── ① 写方上电：模拟量可用，但"是否在更新"应立刻为真 ──
    // Shared() 在新映射上把整个结构清零，padTick=0；随后 SetPadAnalog 写入真实 tick。
    SharedPadAnalog v;
    v.lx = 0.5f; v.ly = -0.25f; v.rx = 0.0f; v.ry = 0.1f; v.lt = 0.8f; v.rt = 0.0f;
    SetPadAnalog(v);

    Check(SharedPadAnalogAlive(), "写方刚发布模拟量后 SharedPadAnalogAlive() 为真");
    Check(SharedKeyAlive() == false || SharedKeyAlive() == true, "SharedKeyAlive() 可安全调用（不崩）");

    // ── ② 读方拿到的是写方发布的值（含符号与量程）──
    SharedPadAnalog rd;
    const bool got = SharedPadAnalogRead(&rd);
    Check(got, "SharedPadAnalogRead() 返回 true（同进程内自读自写）");
    if (got) {
        // 量化误差：short 往返一次约 1/32767
        Check(rd.lx > 0.49f && rd.lx < 0.51f, "lx 往返保真（0.5）");
        Check(rd.ly > -0.26f && rd.ly < -0.24f, "ly 往返保真且符号正确（-0.25）");
        Check(rd.ry > 0.09f && rd.ry < 0.11f, "ry 往返保真（0.1）");
        Check(rd.lt > 0.79f && rd.lt < 0.81f, "lt 往返保真（0.8）");
        Check(rd.rt < 0.005f, "rt 为 0 时往返后仍约等于 0（不出现负数）");
    }

    // ── ③ 关键回归：摇杆活动**不**刷新键鼠 tick，但必须让"模拟量在动"为真 ──
    // 这里模拟 GUI 的判定：推摇杆后，SharedKeyAlive 可能为假（没有键鼠事件），
    // 而 SharedPadAnalogAlive 必须为真。两者取或才是正确的重绘条件。
    v.lx = -0.9f;
    SetPadAnalog(v);
    Check(SharedPadAnalogAlive(), "再次发布模拟量后仍在更新态");
    Check(SharedKeyAlive() || SharedPadAnalogAlive(),
          "重绘条件 (KeyAlive || PadAnalogAlive) 在纯摇杆活动下为真 —— 核心回归点");

    // ── ④ 负值不被截断成 0（早期若用无符号中间量会踩到）──
    v.lx = -1.0f; v.ly = -1.0f; v.rx = -1.0f; v.ry = -1.0f;
    SetPadAnalog(v);
    if (SharedPadAnalogRead(&rd)) {
        Check(rd.lx < -0.99f && rd.ly < -0.99f && rd.rx < -0.99f && rd.ry < -0.99f,
              "满负向摇杆往返保真（-1.0，不被截断）");
    }
    // 满正向
    v.lx = 1.0f; v.ly = 1.0f; v.lt = 1.0f; v.rt = 1.0f;
    SetPadAnalog(v);
    if (SharedPadAnalogRead(&rd)) {
        Check(rd.lx > 0.99f && rd.ly > 0.99f && rd.lt > 0.99f && rd.rt > 0.99f,
              "满正向往返保真（1.0）");
    }

    // ── ⑤ 全 0 发布：面板应能识别"回中"（这是 --padseed 落地渲染的前提）──
    SharedPadAnalog zero;
    SetPadAnalog(zero);
    if (SharedPadAnalogRead(&rd)) {
        Check(rd.lx == 0.0f && rd.ly == 0.0f && rd.rx == 0.0f && rd.ry == 0.0f &&
              rd.lt == 0.0f && rd.rt == 0.0f,
              "发布全 0 后读回全 0（--padseed 的 allZero 判断依赖它）");
    }

    printf("\n通过 %d，失败 %d\n", g_pass, g_fail);
    printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
