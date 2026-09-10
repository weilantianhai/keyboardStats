#include "storage.h"
#include "gui.h"
#include "hook.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmd, int) {
    SetProcessDPIAware();

    // 单实例：自启动 + 手动打开会起两个进程，钩子会重复计数
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"KeyboardStats.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"KeyboardStats 已在运行（请查看系统托盘）", L"KeyboardStats", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    StorageInit();
    bool startHidden = cmd && wcsstr(cmd, L"--background") != nullptr;
    int rc = GuiRun(startHidden);
    StorageFlushNow();
    if (mutex) ReleaseMutex(mutex);
    return rc;
}
