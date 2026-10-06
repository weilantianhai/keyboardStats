// 回归测试：摇杆/扳机必须取自"第一个已连接手柄"，且必须**按值**留下来。
//
// 真实缺陷（2026-09-17，本轮真正的根因）：
// 轮询循环里写的是 `first = &st.Gamepad`——指向循环体内每轮重新构造的局部变量
// XInputState st。等循环结束再读那个指针，它早被后面几轮的 `st = {}` 清零了，
// 于是发布出去的摇杆/扳机**恒为全 0**：面板内圈与两条行程柱永远不动。
//
// 这个 bug 极难靠现象定位，因为：
//   · 按键计数用上升沿、行程用增量，**都是在循环内当场算完的**，所以照样工作
//     ⇒ 手柄按键有反应、看板分数照涨，看起来"手柄是通的"；
//   · 只有模拟量（摇杆位置、扳机行程）在循环外读，于是那一路是死的。
// 这就是"按键能用、摇杆不动"这个反直觉组合的来源。
//
// 修法：各槽位结果按值存进数组，再由 PickFirstConnected 挑出来。
// 这里把"挑第一个已连接"钉死——旧的指针写法在这几条断言下必然现形。
#include "../src/gamepad.h"
#include "../src/hook.h"

#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) { ++g_pass; printf("  ok   %s\n", what); }
    else    { ++g_fail; printf("  FAIL %s\n", what); }
}

static XiGamepad MakePad(int lx, int ly, int rx, int ry, int lt, int rt) {
    XiGamepad p{};
    p.sThumbLX = (short)lx;
    p.sThumbLY = (short)ly;
    p.sThumbRX = (short)rx;
    p.sThumbRY = (short)ry;
    p.bLeftTrigger = (unsigned char)lt;
    p.bRightTrigger = (unsigned char)rt;
    return p;
}

int main() {
    printf("=== 摇杆/扳机取\"第一个已连接手柄\" ===\n");

    // ── ① 手柄在槽位 0，后面三个空 ──
    // 旧写法正是在这里失效：slot 1/2/3 的 `st = {}` 把 slot 0 那份内存清零了。
    {
        bool ok[4] = {true, false, false, false};
        XiGamepad pads[4];
        pads[0] = MakePad(12000, -8000, 3000, -4000, 200, 60);
        XiGamepad out{};
        Check(PickFirstConnected(4, ok, pads, &out), "① 槽位0 有手柄时返回 true");
        Check(out.sThumbLX == 12000 && out.sThumbLY == -8000,
              "① 取到的是槽位0 的左摇杆真值（旧写法这里是 0/0）");
        Check(out.sThumbRX == 3000 && out.sThumbRY == -4000,
              "① 取到的是槽位0 的右摇杆真值");
        Check(out.bLeftTrigger == 200 && out.bRightTrigger == 60,
              "① 取到的是槽位0 的扳机真值");
    }

    // ── ② 手柄在槽位 2（前面空着）：应跳过空槽，取到 2 ──
    {
        bool ok[4] = {false, false, true, false};
        XiGamepad pads[4];
        pads[2] = MakePad(-20000, 7777, 555, -999, 10, 250);
        XiGamepad out{};
        Check(PickFirstConnected(4, ok, pads, &out), "② 槽位2 有手柄时返回 true");
        Check(out.sThumbLX == -20000 && out.sThumbLY == 7777,
              "② 跳过空的槽位0/1，取到槽位2 的真值");
        Check(out.bRightTrigger == 250, "② 槽位2 的 RT 真值");
    }

    // ── ③ 多个手柄同时连着：只认第一个，不能被后面的盖掉 ──
    {
        bool ok[4] = {true, true, true, false};
        XiGamepad pads[4];
        pads[0] = MakePad(1111, 2222, 3333, 4444, 55, 66);
        pads[1] = MakePad(-1, -2, -3, -4, 7, 8);
        pads[2] = MakePad(-9, -8, -7, -6, 5, 4);
        XiGamepad out{};
        Check(PickFirstConnected(4, ok, pads, &out), "③ 多手柄时返回 true");
        Check(out.sThumbLX == 1111 && out.sThumbRX == 3333 && out.bLeftTrigger == 55,
              "③ 取第一个（槽位0），不被槽位1/2 覆盖");
    }

    // ── ④ 一个都没有：返回 false 且不动 out ──
    {
        bool ok[4] = {false, false, false, false};
        XiGamepad pads[4];
        XiGamepad out = MakePad(4321, 4321, 4321, 4321, 9, 9);
        Check(!PickFirstConnected(4, ok, pads, &out), "④ 全空时返回 false");
        Check(out.sThumbLX == 4321 && out.bLeftTrigger == 9,
              "④ 全空时不改写 out（调用方可据此发全 0 让面板回中）");
    }

    // ── ⑤ 边界：0 个槽位、空指针 out 都不能崩 ──
    {
        bool ok[1] = {true};
        XiGamepad pads[1] = {MakePad(7, 7, 7, 7, 1, 1)};
        Check(PickFirstConnected(0, ok, pads, nullptr) == false, "⑤ slotCount=0 时返回 false");
        Check(PickFirstConnected(1, ok, pads, nullptr) == true, "⑤ out 为 nullptr 时仍可安全调用");
    }

    // ── ⑥ 极端值不被截断（short 满量程）──
    {
        bool ok[4] = {true, false, false, false};
        XiGamepad pads[4];
        pads[0] = MakePad(-32768, 32767, -32768, 32767, 255, 0);
        XiGamepad out{};
        PickFirstConnected(4, ok, pads, &out);
        Check(out.sThumbLX == -32768 && out.sThumbLY == 32767 &&
              out.bLeftTrigger == 255 && out.bRightTrigger == 0,
              "⑥ 满量程与 0 都能原样带出");
    }

    // ── ⑦ 端到端：用假手柄跑完整一轮，模拟量必须真的进到共享内存 ──
    // 这才是本轮真正要守的东西：旧的 `first = &st.Gamepad` 在 ① 里只是"返回错"，
    // 而在这里会表现为"发布出去的摇杆值恒为 0"——也就是用户看到的内圈不动。
    {
        struct Fake {
            static unsigned long WINAPI Get(unsigned long slot, XiState* out) {
                *out = XiState{};
                if (slot != 0) return 1167;          // ERROR_DEVICE_NOT_CONNECTED
                out->Gamepad.sThumbLX = 20000;       // 明显超出左摇杆死区 7849
                out->Gamepad.sThumbLY = -12000;
                out->Gamepad.sThumbRX = 15000;       // 右摇杆死区 8689，取明显超出值
                out->Gamepad.sThumbRY = -20000;      // 别取刚过死区的量（扣完死区后所剩无几）
                out->Gamepad.bLeftTrigger = 180;     // 超出扳机阈值 30
                out->Gamepad.bRightTrigger = 0;
                return 0;                            // ERROR_SUCCESS
            }
        };

        // 先发一轮全 0 建立基准（模拟量取的是"每轮的绝对位置"，不是增量）
        SharedPadAnalog zero;
        SetPadAnalog(zero);

        const int n = PollOnceWith(&Fake::Get);
        Check(n == 1, "⑦ 假手柄：识别出 1 个手柄");

        SharedPadAnalog rd;
        if (SharedPadAnalogRead(&rd)) {
            Check(rd.lx > 0.3f, "⑦★ 左摇杆 X 真的发出来了（旧写法这里是 0）");
            Check(rd.ly < -0.1f, "⑦★ 左摇杆 Y 真的发出来了且符号正确");
            Check(rd.rx > 0.1f, "⑦★ 右摇杆 X 真的发出来了");
            Check(rd.ry < -0.3f, "⑦★ 右摇杆 Y 真的发出来了");
            Check(rd.lt > 0.5f, "⑦★ LT 扳机真的发出来了");
            Check(rd.rt == 0.0f, "⑦ RT 未按时为 0");
        } else {
            Check(false, "⑦ 应能读回刚发布的模拟量");
        }
    }

    printf("\n通过 %d，失败 %d\n", g_pass, g_fail);
    printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
