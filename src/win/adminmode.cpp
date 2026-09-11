// 管理员模式实现：
// - 开关本体 = HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
//   下以 exe 全路径为名、数据 "~ RUNASADMIN" 的兼容性标记（Windows 启动该 exe 时读取）。
// - RunningElevated 用进程令牌的 TokenElevation 查询。
// - RelaunchAsAdmin 用 ShellExecute 的 "runas" verb 拉起自己（弹 UAC），本进程退出。
#include "adminmode.h"
#include "autostart.h"
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

namespace {

const wchar_t* kLayersKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

} // namespace

namespace app {

bool RunningElevated() {
    BOOL elevated = FALSE;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        DWORD elev = 0, ret = 0;
        // TokenElevation = 20
        if (GetTokenInformation(tok, (TOKEN_INFORMATION_CLASS)20,
                                &elev, sizeof(elev), &ret))
            elevated = elev;
        CloseHandle(tok);
    }
    return elevated != FALSE;
}

bool ProcessIsElevated(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    HANDLE tok = nullptr;
    bool elevated = false;
    if (OpenProcessToken(h, TOKEN_QUERY, &tok)) {
        DWORD elev = 0, ret = 0;
        if (GetTokenInformation(tok, (TOKEN_INFORMATION_CLASS)20, &elev, sizeof(elev), &ret))
            elevated = elev != 0;
        CloseHandle(tok);
    }
    CloseHandle(h);
    return elevated;
}

bool KillProcess(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    const bool ok = TerminateProcess(h, 1) != 0;
    if (ok) WaitForSingleObject(h, 2000);
    CloseHandle(h);
    return ok;
}

bool AdminModeFlagged() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLayersKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    wchar_t buf[8] = {};
    DWORD type = 0, size = sizeof buf;
    const LONG r = RegQueryValueExW(k, ExePath().c_str(), nullptr, &type,
                                    (BYTE*)buf, &size);
    RegCloseKey(k);
    // 值数据形如 "~ RUNASADMIN"，以 RUNASADMIN 结尾即视为开启
    return r == ERROR_SUCCESS && type == REG_SZ;
}

bool SetAdminModeFlagged(bool enable) {
    bool ok = true;
    HKEY k;
    const DWORD create = KEY_SET_VALUE | KEY_QUERY_VALUE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kLayersKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, create, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    if (enable) {
        const std::wstring v = L"~ RUNASADMIN";
        ok = RegSetValueExW(k, ExePath().c_str(), 0, REG_SZ,
                            (const BYTE*)v.c_str(),
                            (DWORD)((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        RegDeleteValueW(k, ExePath().c_str());   // 本来就不存在也视为成功
    }
    RegCloseKey(k);
    return ok;
}

bool RelaunchAsAdmin() {
    const std::wstring exe = ExePath();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof sei;
    sei.lpVerb = L"runas";          // 触发 UAC；用户取消时返回 FALSE
    sei.lpFile = exe.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    return true;
}

int KillOtherInstances() {
    // 终结同 exe 的其它进程（普通权限的旧记录进程/GUI）；
    // 提权实例（高完整性）可以终结普通权限实例，反之不行。
    int killed = 0;
    const unsigned long self = GetCurrentProcessId();
    // 用 Process32 走快照
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == self) continue;
            if (_wcsicmp(pe.szExeFile, L"KeyboardStats.exe") != 0) continue;
            HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                                   FALSE, pe.th32ProcessID);
            if (!h) continue;
            if (ProcessIsElevated(pe.th32ProcessID)) { CloseHandle(h); continue; }
            if (TerminateProcess(h, 1)) {
                WaitForSingleObject(h, 2000);
                ++killed;
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return killed;
}

} // namespace app
