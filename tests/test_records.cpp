// 记录管理逻辑测试：描述 / 导出 / 数据文件标记与识别 / 转入并切换 / 新建 / 搬文件夹 / 清除
// 运行前设置 KEYBOARDSTATS_DIR 指向临时目录（不触碰真实数据）
#include "../src/storage.h"
#include "../src/timeutil.h"

#include <windows.h>

#include <cstdio>
#include <string>

static const char* KindName(DataFileKind k) {
    switch (k) {
        case DataFileKind::Marked: return "带标记";
        case DataFileKind::Legacy: return "无标记但全是事件（旧版兼容）";
        default: return "不是数据文件";
    }
}

static void printInfo(const char* tag) {
    StorageInfo i = StorageDescribe();
    printf("[%s] events=%ld files=%d bytes=%lld 范围=%u~%u\n",
           tag, i.events, i.files, i.bytes, i.firstYmd, i.lastYmd);
}

// 写一个假文件（比如 CSV），用来验证"非法数据文件"的识别
static void writeFake(const wchar_t* path, const char* text) {
    if (FILE* f = _wfopen(path, L"wb")) {
        fputs(text, f);
        fclose(f);
    }
}

int main() {
    StorageInit();
    printInfo("初始（自动按月）");
    printf("[数据文件夹] %ls\n", DataFolderPath().c_str());
    printf("[数据文件]   %ls\n", DataFilePath().c_str());
    printf("[标记行]     %s", StorageMagicLine());
    printf("[识别:按月文件] %s\n", KindName(StorageProbeFile(DataFilePath())));

    long n = 0;
    const bool okJsonl = StorageExportJsonl(L"export-test.jsonl", &n);
    printf("[导出JSONL] ok=%d count=%ld 识别=%s（导出文件应带标记）\n",
           (int)okJsonl, n, KindName(StorageProbeFile(L"export-test.jsonl")));
    n = 0;
    const bool okCsv = StorageExportCsv(L"export-test.csv", &n);
    printf("[导出CSV] ok=%d count=%ld 识别=%s（应为不是数据文件）\n",
           (int)okCsv, n, KindName(StorageProbeFile(L"export-test.csv")));

    // 非法文件必须被拒（错误文本是中文，这里只打印是否非空，避免控制台编码干扰）
    writeFake(L"not-data.jsonl", "name,age\nTom,3\n");
    std::wstring err;
    const long bad = StorageAdoptJsonl(L"not-data.jsonl", &err);
    printf("[转入非法文件] 返回=%ld（应为 -1） 提示非空?%d 文件未被搬走?%d\n",
           bad, (int)!err.empty(),
           (int)(GetFileAttributesW(L"not-data.jsonl") != INVALID_FILE_ATTRIBUTES));

    // 正常转入：文件应被移动、带标记、成为当前数据文件
    const long adopted = StorageAdoptJsonl(L"export-test.jsonl", &err);
    printf("[转入] count=%ld 源文件还在? %d（应 0） 当前=%ls\n",
           adopted, (int)(GetFileAttributesW(L"export-test.jsonl") != INVALID_FILE_ATTRIBUTES),
           DataFileName().c_str());
    printInfo("转入后");

    // 新建数据文件：带标记的空文件
    std::wstring created;
    if (StorageCreateDataFile(&created, &err)) {
        printf("[新建] %ls 识别=%s\n", created.c_str(), KindName(StorageProbeFile(DataFilePath())));
        printInfo("新建后（应为 0）");
    } else {
        printf("[新建] 失败：%ls\n", err.c_str());
    }

    printf("[文件夹内数据文件] %d 个\n",
           (int)DataFilesInFolder(DataFolderPath()).size());

    // 无权限 / 建不出来的文件夹：必须直接失败并给出原因，且不改变当前文件夹
    printf("[可写预检] 临时目录可写? %d（应 1） 不存在路径可写? %d（应 0）\n",
           (int)CanWriteToFolder(DataFolderPath()),
           (int)CanWriteToFolder(L"Q:\\no-such-drive-kbstats"));
    const std::wstring beforeDir = DataFolderPath();
    FolderSwitchResult badResult{};
    const bool badOk = StorageSetDataFolder(L"Q:\\no-such-drive-kbstats", false, &err, &badResult);
    printf("[切到无效文件夹] ok=%d（应 0） 提示非空?%d 文件夹没变?%d\n",
           (int)badOk, (int)!err.empty(), (int)(DataFolderPath() == beforeDir));

    // 切换文件夹 + 一起搬运（此时当前数据文件是刚新建的那个，走"显式文件一起搬"的路径）
    const std::wstring sub = DataFolderPath() + L"\\moved";
    FolderSwitchResult r{};
    const bool folderOk = StorageSetDataFolder(sub, true, &err, &r);
    printf("[换文件夹+搬运] ok=%d moved=%d failed=%d（moved 应为 3）\n",
           (int)folderOk, r.moved, r.failed);
    printf("[搬运后当前文件] 名字保持? %d 文件在? %d\n",
           (int)(DataFileName() == created),
           (int)(GetFileAttributesW(DataFilePath().c_str()) != INVALID_FILE_ATTRIBUTES));
    printInfo("搬运后（当前仍是新建的空文件，应为 0）");

    // 切回自动：搬运过来的按月文件应继续被识别
    if (StorageSetDataFile(L"", &err)) {
        printf("[恢复自动] 当前=%ls\n", DataFilePath().c_str());
        printInfo("恢复自动后（应为 1864 上下）");
    }

    StorageClearAll();
    printInfo("清除后（应为 0）");
    return 0;
}
