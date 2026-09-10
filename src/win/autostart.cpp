// 开机自启动：注册表 HKCU Run 键读写（从旧 gui.cpp 平移，逻辑不变）
#include "autostart.h"
#include <windows.h>

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunVal = L"KeyboardStats";

} // namespace

std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

bool AutostartEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type, size = 0;
    LONG r = RegQueryValueExW(k, kRunVal, nullptr, &type, nullptr, &size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

bool AutostartSet(bool enable) {
    HKEY k;
    // 写回核验要用同句柄查询，所以打开时把 KEY_QUERY_VALUE 一起要上
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    bool ok = true;
    if (enable) {
        // 带 --record：开机直接启动无界面记录进程（钩子+落盘+托盘图标），内存占用极小
        std::wstring v = L"\"" + ExePath() + L"\" --record";
        ok = RegSetValueExW(k, kRunVal, 0, REG_SZ, (const BYTE*)v.c_str(),
                            (DWORD)((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        // 写回核验：确认真的可查询到（防静默失败）
        if (ok) {
            DWORD type = 0, size = 0;
            ok = RegQueryValueExW(k, kRunVal, nullptr, &type, nullptr, &size) == ERROR_SUCCESS
                 && type == REG_SZ;
        }
    } else {
        RegDeleteValueW(k, kRunVal);   // 值本来就不存在时也视为成功
    }
    RegCloseKey(k);
    return ok;
}
