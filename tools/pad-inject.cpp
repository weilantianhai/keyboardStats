// 手柄模拟量注入器（开发自用，不进正式产物）。
//
// 用途：在没有真手柄的机器上，把一组摇杆/扳机数值写进共享内存
// Local\KeyboardStats.KeyState3，让预览构建走**真实的读取路径**
// （SharedPadAnalogRead）而不是 --padseed 的兜底分支。
// 关键价值：注入器可以在 GUI 运行期间**改值**，用来验证
// "数值变了界面是否跟着重绘"——这正是 --padseed 固定值测不出来的那件事。
//
// 用法：
//   pad-inject.exe <控制文件路径>
// 控制文件里每行 6 个数字：lx ly rx ry lt rt（-1..1 / 0..1）。
// 写成 quit 则退出。文件不存在/格式错时注入全 0（内圈回中）。
//
// 编译（MinGW g++，与 pad-diag 同）：
//   g++ -O2 -municode -o build/pad-inject.exe tools/pad-inject.cpp -luser32

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
// 必须与 src/hook.cpp 的 SharedState **逐字段一致**（本机两处都是 4 字节 unsigned long）
struct SharedState {
    unsigned long tick;
    unsigned char state[512];
    unsigned long padTick;
    short lx, ly, rx, ry;
    unsigned char lt, rt;
};
constexpr wchar_t kMapName[] = L"Local\\KeyboardStats.KeyState3";

short toShort(float f) {
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return (short)(f * 32767.0f);
}
unsigned char toByte(float f) {
    if (f > 1.0f) f = 1.0f;
    if (f < 0.0f) f = 0.0f;
    return (unsigned char)(f * 255.0f + 0.5f);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"用法：pad-inject.exe <控制文件路径>\n");
        return 2;
    }
    const wchar_t* ctrl = argv[1];

    SECURITY_ATTRIBUTES sa{};
    SECURITY_DESCRIPTOR sd{};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                    0, sizeof(SharedState), kMapName);
    if (!map) { fwprintf(stderr, L"CreateFileMapping 失败 %lu\n", GetLastError()); return 1; }
    SharedState* s = (SharedState*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState));
    if (!s) { fwprintf(stderr, L"MapViewOfFile 失败 %lu\n", GetLastError()); return 1; }

    fwprintf(stderr, L"pad-inject 就绪（sizeof=%zu，padTick@%zu）：%s\n",
             sizeof(SharedState), offsetof(SharedState, padTick), ctrl);

    for (;;) {
        float v[6] = {0, 0, 0, 0, 0, 0};
        FILE* f = _wfopen(ctrl, L"rb");
        if (f) {
            char line[256] = {0};
            if (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "quit", 4) == 0) { fclose(f); break; }
                float t[6] = {0, 0, 0, 0, 0, 0};
                if (sscanf_s(line, "%f %f %f %f %f %f",
                             &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) == 6) {
                    for (int i = 0; i < 6; ++i) v[i] = t[i];
                }
            }
            fclose(f);
        }
        s->lx = toShort(v[0]); s->ly = toShort(v[1]);
        s->rx = toShort(v[2]); s->ry = toShort(v[3]);
        s->lt = toByte(v[4]);   s->rt = toByte(v[5]);
        s->padTick = GetTickCount();
        Sleep(40);
    }

    UnmapViewOfFile(s);
    CloseHandle(map);
    return 0;
}
