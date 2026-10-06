// 诊断：StorageDescribe 改读内存后，数值口径与性能对比。
//
// 背景：StorageDescribe() 曾被 DrawBoard 每帧调用，实现是把**所有数据文件逐行读完**
// 再逐行解析——10MB / 26 万行时单帧几十到上百毫秒。现在改成读内存聚合表 +
// 复用增量重载维护的文件尺寸快照。
//
// 这里做两件事：
//   ① 口径核对：新实现 vs 独立重算（把老算法照抄一遍跑），逐项比对
//   ② 性能核对：单次调用的平均耗时
//
// 不是硬断言的回归测试（耗时受机器影响），而是**给人看结论**的诊断程序。
// 用法：g++ test_describe_perf.cpp storage.cpp timeutil.cpp -o x.exe
//       然后直接运行（会用真实的 KEYBOARDSTATS 偏好与数据目录）
#include "../src/storage.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <chrono>
#include <cmath>

// 按通配符扫目录（DataFiles/TravelFiles 是 storage.cpp 内部 static，这里自己扫）
static std::vector<std::wstring> Scan(const std::wstring& dir, const wchar_t* pat) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\" + pat).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// ── 老算法的独立重算（照抄改版前的实现，用来核对口径）──
static void OldAlgorithm(long* events, int* files, long long* bytes) {
    *events = 0; *files = 0; *bytes = 0;
    for (const std::wstring& p : Scan(DataFolderPath(), L"*.jsonl")) {
        if (p.find(L"analog-") != std::wstring::npos) continue;   // 行程文件下一轮单独算
        ++*files;
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a))
            *bytes += ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        FILE* f = _wfopen(p.c_str(), L"rb");
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            const char* tp = strstr(line, "\"t\":\"");
            if (!tp || line[0] != '{') continue;
            int y, mo, d, h, mi, s, ms;
            if (sscanf(tp + 5, "%d-%d-%dT%d:%d:%d.%d", &y, &mo, &d, &h, &mi, &s, &ms) != 7)
                continue;
            ++*events;
        }
        fclose(f);
    }
    for (const std::wstring& p : Scan(DataFolderPath(), L"analog-*.jsonl")) {
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) {
            *bytes += ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
            ++*files;
        }
    }
}

int main() {
    printf("=== StorageDescribe 口径与耗时核对 ===\n");

    // 必须先 StorageInit：数据目录是 ui-data.txt 的 dir= 指定的，LoadDataPrefs 在
    // StorageInit 里才执行——在此之前 DataFolderPath() 会回落到默认目录，扫了个空。
    const auto t0 = std::chrono::steady_clock::now();
    StorageInit();
    const auto t1 = std::chrono::steady_clock::now();
    printf("数据目录: %ls\n", DataFolderPath().c_str());
    printf("StorageInit（全量加载）: %.1f ms\n\n",
           std::chrono::duration<double, std::milli>(t1 - t0).count());

    // ① 老算法独立重算（逐行读盘）
    long oe = 0; int of = 0; long long ob = 0;
    const auto t2 = std::chrono::steady_clock::now();
    OldAlgorithm(&oe, &of, &ob);
    const auto t3 = std::chrono::steady_clock::now();
    const double oldMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // ② 新实现（读内存快照）
    const StorageInfo ni = StorageDescribe();

    // ③ 新实现耗时（20 次平均）
    const int kRuns = 20;
    const auto t4 = std::chrono::steady_clock::now();
    for (int i = 0; i < kRuns; ++i) { volatile StorageInfo s = StorageDescribe(); (void)s; }
    const auto t5 = std::chrono::steady_clock::now();
    const double newMs = std::chrono::duration<double, std::milli>(t5 - t4).count() / kRuns;

    printf("口径比对            新实现(内存)      老实现(逐行读盘)      一致?\n");
    printf("  事件总数      %14ld  %20ld          %s\n", ni.events, oe,
           ni.events == oe ? "是" : "★不一致");
    printf("  文件数        %14d  %20d          %s\n", ni.files, of,
           ni.files == of ? "是" : "★不一致");
    printf("  占用字节      %14lld  %20lld      %s\n", ni.bytes, ob,
           ni.bytes == ob ? "是" : "★不一致");
    printf("  最早/最近日   %u ~ %u\n\n", ni.firstYmd, ni.lastYmd);

    printf("单次耗时            老实现 %8.2f ms   新实现 %8.4f ms   提速 %.0f 倍\n",
           oldMs, newMs, oldMs > 0.001 ? oldMs / std::max(newMs, 1e-6) : 0.0);

    // 事件总数允许小幅时序差：记录进程每 5 秒落盘，若恰好落在两次读取之间，
    // 磁盘行数会**比内存快照多几行**（内存是加载那一刻的快照）——这是时序，不是口径。
    const long diff = oe - ni.events;
    const bool ok = (ni.files == of) && (ni.bytes == ob) && (diff >= 0 && diff < 50);
    printf("\n结论: %s（事件差 %ld 行属落盘时序差）\n", ok ? "口径一致 ✓" : "口径有差异 ✗");
    return ok ? 0 : 1;
}
