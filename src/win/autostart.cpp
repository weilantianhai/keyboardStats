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

void AutostartSet(bool enable) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (enable) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, kRunVal, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunVal);
    }
    RegCloseKey(k);
}
