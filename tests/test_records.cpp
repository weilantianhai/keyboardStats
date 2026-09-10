// 记录管理逻辑测试：描述 / 导出 JSONL+CSV / 导入 / 清除
// 运行前设置 KEYBOARDSTATS_DIR 指向临时目录（不触碰真实数据）
#include "storage.h"

#include <cstdio>
#include <string>

static void printInfo(const char* tag) {
    StorageInfo i = StorageDescribe();
    printf("[%s] events=%ld files=%d bytes=%lld 范围=%u~%u\n",
           tag, i.events, i.files, i.bytes, i.firstYmd, i.lastYmd);
}

int main() {
    StorageInit();
    printInfo("初始");

    long n = 0;
    const bool okJsonl = StorageExportJsonl(L"export-test.jsonl", &n);
    printf("[导出JSONL] ok=%d count=%ld\n", (int)okJsonl, n);
    n = 0;
    const bool okCsv = StorageExportCsv(L"export-test.csv", &n);
    printf("[导出CSV] ok=%d count=%ld\n", (int)okCsv, n);

    if (FILE* f = _wfopen(L"export-test.csv", L"rb")) {
        char line[256];
        printf("[CSV 前 3 行]\n");
        for (int i = 0; i < 3 && fgets(line, sizeof line, f); ++i) printf("  %s", line);
        fclose(f);
    }

    std::wstring err;
    const long imported = StorageImportJsonl(L"export-test.jsonl", &err);
    printf("[导入] count=%ld err=空?%d\n", imported, (int)err.empty());
    printInfo("导入后（应为初始的两倍）");

    const long again = StorageImportJsonl(L"no-such-file.jsonl", &err);
    printf("[导入不存在的文件] 返回=%ld（应为 -1） err非空?%d\n", again, (int)!err.empty());

    StorageClearAll();
    printInfo("清除后（应为 0）");
    return 0;
}
