// 手柄实时链路诊断工具（开发自用，不进正式产物）。
//
// 为什么要有它：这条路总共就三跳——XInput 读硬件 → 写方发布到共享内存 → GUI 读来画。
// 任一跳断了，界面表现**完全一样**（内圈不动），光看界面没法分辨。这个工具把三跳
// 各自的值一次性摊开，直接指出断在哪。
//
//   ① XInput 能不能加载、有几个手柄在槽位里、原始摇杆/扳机值是多少
//   ② 三个版本的共享内存各存不存在（KeyState3 / KeyState2 / KeyState）
//   ③ v3 里的 padTick 有没有在跳、六个模拟量是多少
//
// 注意 --padiag 那种方案依赖命令行传参，双击 exe 根本传不了，所以诊断必须做成
// **双击即出结论**的形式：结果写 UTF-8 文件到桌面，再用 MessageBox 弹一句摘要。
//
// 构建（不依赖项目其它源文件）：
//   "D:\Program Files\mingw64\bin\g++.exe" -std=c++17 -O2 -municode \
//       tools/pad-diag.cpp -o build/pad-diag.exe -luser32 -lole32
//
// 用法：让 KeyboardStats 跑起来（托盘里有记录进程），然后**双击** pad-diag.exe。
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// 与 src/hook.cpp 的 SharedState 必须逐字段一致
constexpr int kKeySlots = 512;
struct SharedState {
    unsigned long tick;
    unsigned char state[kKeySlots];
    unsigned long padTick;
    short lx, ly, rx, ry;
    unsigned char lt, rt;
};

// 只看布局前半段时的字节数（各版本的实际大小不同，映射时必须按版本裁量）
size_t BytesFor(int version) {
    if (version >= 3) return sizeof(SharedState);
    return sizeof(unsigned long) + (size_t)(version == 1 ? 256 : kKeySlots);
}

struct PadView {
    bool exists = false;
    int version = 0;
    unsigned long tick = 0;
    unsigned long padTick = 0;
    short lx = 0, ly = 0, rx = 0, ry = 0;
    unsigned char lt = 0, rt = 0;
};

PadView Probe(const wchar_t* name, int version) {
    PadView out;
    out.version = version;
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return out;                       // 不存在：保持 exists=false
    out.exists = true;
    if (const void* p = MapViewOfFile(h, FILE_MAP_READ, 0, 0, BytesFor(version))) {
        const SharedState* s = (const SharedState*)p;
        out.tick = s->tick;
        if (version >= 3) {
            out.padTick = s->padTick;
            out.lx = s->lx; out.ly = s->ly;
            out.rx = s->rx; out.ry = s->ry;
            out.lt = s->lt; out.rt = s->rt;
        }
        UnmapViewOfFile((LPVOID)p);
    }
    CloseHandle(h);
    return out;
}

// ── XInput 动态加载（与 src/gamepad.cpp 同样的手法）──
struct XiGamepad {
    unsigned short wButtons;
    unsigned char bLeftTrigger, bRightTrigger;
    short sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XiState { unsigned long dwPacketNumber; XiGamepad Gamepad; };
using XiGetState = unsigned long(WINAPI*)(unsigned long, XiState*);

XiGetState g_xi = nullptr;
HMODULE g_xiLib = nullptr;

bool LoadXi() {
    for (const wchar_t* n : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        if (HMODULE h = LoadLibraryW(n)) {
            if (auto fn = (XiGetState)GetProcAddress(h, "XInputGetState")) {
                g_xiLib = h;
                g_xi = fn;
                return true;
            }
            FreeLibrary(h);
        }
    }
    return false;
}

}  // namespace

int wmain() {
    const DWORD now0 = GetTickCount();

    // 结果写到桌面 UTF-8 文件：控制台是 GBK，中文从控制台复制容易乱码。
    std::wstring path;
    {
        wchar_t profile[MAX_PATH] = {};
        GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        path = std::wstring(profile) + L"\\Desktop\\手柄诊断报告.txt";
    }
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) path = L"手柄诊断报告.txt", f = _wfopen(path.c_str(), L"wb");
    if (!f) { MessageBoxW(nullptr, L"写不了结果文件", L"手柄诊断", MB_ICONERROR); return 1; }
    fputs("\xEF\xBB\xBF", f);   // UTF-8 BOM

    auto line = [&](const char* fmt, ...) {
        char buf[1024];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        fputs(buf, f); fputc('\n', f);
    };

    line("手柄实时链路诊断报告");
    line("时间：%s", "（见文件修改时间）");
    line("");

    // ── ① XInput 直读 ──
    line("【① XInput 直连：越过本程序，问 Windows 自己】");
    const bool xiOk = LoadXi();
    int slots = 0;
    XiState st0{};
    int slot0 = -1;
    if (!xiOk) {
        line("  XInput 加载失败（xinput1_4 / 1_3 / 9_1_0 都没 load 起来）");
    } else {
        for (int i = 0; i < 4; ++i) {
            XiState st{};
            if (g_xi((unsigned long)i, &st) == 0) {
                ++slots;
                if (slot0 < 0) { slot0 = i; st0 = st; }
            }
        }
        line("  XInput 加载成功，检测到 %d 个手柄", slots);
        if (slot0 >= 0) {
            line("  第一个手柄在槽位 %d", slot0);
            line("    原始 sThumbLX=%d LY=%d RX=%d RY=%d", st0.Gamepad.sThumbLX,
                 st0.Gamepad.sThumbLY, st0.Gamepad.sThumbRX, st0.Gamepad.sThumbRY);
            line("    原始 bLeftTrigger=%u bRightTrigger=%u  wButtons=0x%04X",
                 st0.Gamepad.bLeftTrigger, st0.Gamepad.bRightTrigger, st0.Gamepad.wButtons);
        }
    }
    line("");

    // ── ② 三个版本的共享内存 ──
    line("【② 共享内存：本程序内部传输用的那一块】");
    PadView v3 = Probe(L"Local\\KeyboardStats.KeyState3", 3);
    PadView v2 = Probe(L"Local\\KeyboardStats.KeyState2", 2);
    PadView v1 = Probe(L"Local\\KeyboardStats.KeyState", 1);
    line("  KeyState3（v3，含模拟量）：%s", v3.exists ? "存在" : "不存在");
    line("  KeyState2（v2，旧）：      %s", v2.exists ? "存在" : "不存在");
    line("  KeyState  （v1，更旧）：   %s", v1.exists ? "存在" : "不存在");
    if (v3.exists) {
        line("  v3 内容： 按键 tick 距今 %lu ms", now0 - v3.tick);
        line("            模拟量 padTick 距今 %lu ms", now0 - v3.padTick);
        line("            lx=%d ly=%d rx=%d ry=%d LT=%u RT=%u",
             v3.lx, v3.ly, v3.rx, v3.ry, v3.lt, v3.rt);
        line("            归一化后 lx=%+.3f ly=%+.3f LT=%.3f RT=%.3f",
             v3.lx / 32767.0f, v3.ly / 32767.0f, v3.lt / 255.0f, v3.rt / 255.0f);
    }
    if (v2.exists || v1.exists) {
        line("  ⚠ 旧版本映射仍在系统里 —— 只要有进程持有它，GUI 就有可能被它截住");
    }
    line("");

    // ── ③ 观察 padTick 是否在跳 ──
    int changes = 0;
    unsigned long lastPad = v3.padTick;
    if (v3.exists) {
        line("【③ 追踪 3 秒：padTick 有没有持续更新】");
        const DWORD t0 = GetTickCount();
        while (GetTickCount() - t0 < 3000) {
            PadView cur = Probe(L"Local\\KeyboardStats.KeyState3", 3);
            if (cur.exists && cur.padTick != lastPad) { ++changes; lastPad = cur.padTick; }
            Sleep(120);
        }
        line("  3 秒内 padTick 变化 %d 次", changes);
        PadView fin = Probe(L"Local\\KeyboardStats.KeyState3", 3);
        line("  最新 lx=%d ly=%d LT=%u RT=%u", fin.lx, fin.ly, fin.lt, fin.rt);
        line("");
    }

    // ── 结论 ──
    std::wstring head, body;
    if (!xiOk) {
        head = L"XInput 加载失败";
        body = L"系统缺少 XInput 运行库，手柄采集线程起不来，摇杆永远不会有数据。";
    } else if (slots == 0) {
        head = L"系统没检测到 Xbox 手柄";
        body = L"XInput 可用但四个槽位都是空的。请确认手柄已连接并被识别（此期间不要进 Steam 大屏幕模式之类的会抢占手柄的程序）。";
    } else if (!v3.exists) {
        head = L"KeyState3 不存在";
        body = L"记录进程没在建共享内存：可能托盘里的记录进程没起来，或者它还驻留着旧版本代码。请从托盘彻底退出程序后重新启动。";
    } else if (v2.exists || v1.exists) {
        head = L"检测到旧版本共享内存残留";
        body = L"旧版本 KeyState 映射仍在，GUI 有可能被它截住而读不到模拟量。请从托盘彻底退出全部 KeyboardStats 实例再启动。";
    } else if (changes == 0) {
        head = L"padTick 没有更新";
        body = L"记录进程没在发布模拟量。手柄虽被识别，但写方没把值写进共享内存。";
    } else if (v3.lx == 0 && v3.ly == 0 && v3.rx == 0 && v3.ry == 0 && v3.lt == 0 && v3.rt == 0) {
        head = L"发布的全是 0";
        body = L"数据通路在写、但值恒为 0：手柄此刻没有推动，或 NormalizeStick 的死区把值吃掉了。推着摇杆再跑一次看看。";
    } else {
        head = L"数据已到共享内存";
        body = L"三跳都是通的。若界面仍不动，请把这份报告连同窗口截图一起反馈。";
    }
    line("【结论】");
    std::string narrowHead(head.begin(), head.end());
    line("  %s", narrowHead.c_str());

    fclose(f);

    std::wstring msg = L"结论：" + head + L"\n\n" + body +
                       L"\n\n详细报告已保存到：\n" + path;
    MessageBoxW(nullptr, msg.c_str(), L"手柄诊断", MB_OK | MB_ICONINFORMATION);
    return 0;
}
