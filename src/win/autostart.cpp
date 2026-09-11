// 开机自启动：计划任务（登录触发 + 最高权限）。
// 相比注册表 Run 键的优势：
//   1. 任务计划程序服务以「最高权限」静默启动记录进程——开机自启动不弹 UAC；
//   2. 提权后的钩子是高完整性级别，管理员窗口、反作弊游戏（如 Valorant/原神启动器）
//      等高完整性窗口获得焦点时也能正常记录（普通权限钩子会被 UIPI 屏蔽）。
// 创建/删除任务本身需要管理员权限（本程序经 manifest 要求管理员运行，天然满足）。
// 兼容：会自动清理旧版注册表 Run 键方式，避免双重启动。
#include "autostart.h"
#include "adminmode.h"
#include <windows.h>
#include <cstdlib>

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunVal = L"KeyboardStats";

// 旧版注册表方式的清理（迁移 + 关闭时都要做）
void RemoveLegacyRegistry() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, kRunVal);
    RegCloseKey(k);
}

} // namespace

std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

bool AutostartEnabled() {
    // 计划任务查询不需要管理员权限；exit code：0=存在 1=不存在
    if (_wsystem(L"schtasks /Query /TN \"KeyboardStats Recorder\" > NUL 2>&1") == 0)
        return true;
    // 兼容旧版注册表方式
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type, size = 0;
    LONG r = RegQueryValueExW(k, kRunVal, nullptr, &type, nullptr, &size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

bool AutostartSet(bool enable) {
    if (enable) {
        // /RL HIGHEST：以最高权限运行（任务计划服务静默提权，开机不弹 UAC，游戏内也可记录）
        std::wstring cmd = L"schtasks /Create /F /TN \"KeyboardStats Recorder\" /TR \"\\\""
                           + ExePath() + L"\\\" --record\" /SC ONLOGON /RL HIGHEST";
        const bool ok = (_wsystem(cmd.c_str()) == 0);
        RemoveLegacyRegistry();   // 迁移：清掉旧注册表自启动，避免双重启动
        return ok;
    }
    // 关闭：删任务（可能本来就不存在）+ 清旧注册表
    _wsystem(L"schtasks /Delete /F /TN \"KeyboardStats Recorder\" > NUL 2>&1");
    RemoveLegacyRegistry();
    return true;
}
