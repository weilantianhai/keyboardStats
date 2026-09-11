// 开机自启动：计划任务（登录触发 + 最高权限）。
// 相比注册表 Run 键的优势：
//   1. 任务计划程序服务以「最高权限」静默启动记录进程——开机自启动不弹 UAC；
//   2. 提权后的钩子是高完整性级别，管理员窗口、反作弊游戏（如 Valorant/原神启动器）
//      等高完整性窗口获得焦点时也能正常记录（普通权限钩子会被 UIPI 屏蔽）。
// 性能红线：**渲染路径（AutostartEnabled）绝不能启动子进程**——_wsystem 每次调用
// 都会 spawn cmd.exe（阻塞 UI 数百毫秒且闪现控制台窗口）。任务存在性改查
// System32\Tasks 下的任务定义文件（微秒级）。schtasks 仅在开关切换时执行一次，
// 且用 CREATE_NO_WINDOW 隐藏，杜绝闪窗。
// 兼容：会自动清理旧版注册表 Run 键方式，避免双重启动。
#include "autostart.h"
#include "adminmode.h"
#include <windows.h>

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunVal = L"KeyboardStats";
const wchar_t* kTaskName = L"KeyboardStats Recorder";
// Windows 把计划任务存为该目录下的 XML；存在即任务已注册
const wchar_t* kTaskFile = L"C:////Windows////System32////Tasks////KeyboardStats Recorder";

// 旧版注册表方式的清理（迁移 + 关闭时都要做）
void RemoveLegacyRegistry() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, kRunVal);
    RegCloseKey(k);
}

// 隐藏窗口执行命令并等待完成；返回 exit code == 0
bool RunHidden(const std::wstring& cmd) {
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring c = cmd;
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

} // namespace

std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

bool AutostartEnabled() {
    // 渲染路径安全：纯文件属性检查，无子进程
    if (GetFileAttributesW(kTaskFile) != INVALID_FILE_ATTRIBUTES) return true;
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
        // 管理员模式/提权环境下用 /RL HIGHEST（静默提权、游戏内也可记录）；
        // 普通权限下创建默认（有限）权限任务——后台记录正常，游戏内不记录。
        const bool wantHighest = app::AdminModeFlagged();
        std::wstring cmd = L"schtasks /Create /F /TN \"KeyboardStats Recorder\" /TR \\\""
                           + ExePath() + L"\\\" --record\" /SC ONLOGON";
        if (wantHighest) cmd += L" /RL HIGHEST";
        bool ok = RunHidden(cmd);
        if (!ok && wantHighest) {
            // 未提权时创建 HIGHEST 任务会失败：降级为普通权限任务
            // （后台记录正常；用户重启进管理员模式后由启动逻辑自动升级）
            std::wstring plain = L"schtasks /Create /F /TN \"KeyboardStats Recorder\" /TR \\\""
                                 + ExePath() + L"\\\" --record\" /SC ONLOGON";
            ok = RunHidden(plain);
        }
        RemoveLegacyRegistry();   // 迁移：清掉旧注册表自启动，避免双重启动
        return ok;
    }
    RunHidden(L"schtasks /Delete /F /TN \"KeyboardStats Recorder\"");
    RemoveLegacyRegistry();
    return true;
}
