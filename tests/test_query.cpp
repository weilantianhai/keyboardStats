// 测试程序：验证 QueryRange 各模式的桶结构与计数（不启动 GUI）
// 编译：g++ -std=c++17 -DTEST_MAIN=1 -o test_query.exe tests\test_query.cpp src\storage.cpp src\timeutil.cpp
#include "../src/storage.h"
#include "../src/timeutil.h"
#include <windows.h>
#include <cstdio>

int main() {
    StorageInit();

    RangeStats all = QueryRange(3, 0, 0);
    wprintf(L"[全部] total=%ld 桶数=%zu 桶标签=%ls 桶值=%ld\n",
            all.total, all.buckets.size(),
            all.buckets.empty() ? L"-" : all.buckets[0].label.c_str(),
            all.buckets.empty() ? -1 : all.buckets[0].count);

    RangeStats today = QueryRange(0, 0, 0);
    wprintf(L"[今天] total=%ld 桶数=%zu\n", today.total, today.buckets.size());
    for (size_t i = 0; i < today.buckets.size(); ++i)
        if (today.buckets[i].count > 0)
            wprintf(L"  %ls → %ld\n", today.buckets[i].label.c_str(), today.buckets[i].count);

    long sum = 0;
    const char* names[256] = {};
    for (int vk = 0; vk < 256; ++vk) {
        if (all.counts[vk] > 0) sum += all.counts[vk];
    }
    printf("[计数] 非零键数=%ld 总和=%ld  空格=%ld A=%ld Z=%ld G=%ld\n",
           [&]{ long c=0; for(int v=0;v<256;++v) if(all.counts[v]>0) ++c; return c; }(),
           sum, all.counts[32], all.counts['A'], all.counts['Z'], all.counts['G']);
    return 0;
}
