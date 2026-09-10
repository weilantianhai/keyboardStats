// 只验证 DefaultDataFolder() 的解析：不调用 StorageInit，不碰任何偏好文件。
// 编译：g++ -std=c++17 -O2 tests/test_default_dir.cpp src/storage.cpp src/timeutil.cpp -o test_default_dir.exe
#include "../src/storage.h"

#include <windows.h>

#include <cstdio>

int main() {
    const std::wstring def = DefaultDataFolder();
    const DWORD attr = GetFileAttributesW(def.c_str());
    const bool isDir = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool writable = CanWriteToFolder(def);

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wprintf(L"[exe]        %ls\n", exe);
    wprintf(L"[默认数据文件夹] %ls\n", def.c_str());
    wprintf(L"[存在且是目录] %d（应 1）  可写? %d（应 1）\n", (int)isDir, (int)writable);
    return 0;
}
