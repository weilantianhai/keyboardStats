// 开机自启动：计划任务（登录触发）。
// 为什么不用注册表 Run 键：Run 键项带提权兼容标记（RUNASADMIN）时，登录阶段的
// 提权请求会被 Windows 静默丢弃（Consent 服务未就绪），表现为"在自启动列表里
// 却开机不启动"——本机已实测确认。计划任务的最高权限由任务计划程序服务授予，
// 不依赖登录时的 Consent 流程。
// 为什么用 XML 注册而非 /TR 参数：schtasks 的 /TR 对内嵌引号有经典转义坑
// （路径带引号+参数时部分系统直接报参数错误）。XML 的 Command/Arguments 是
// 元素文本，无转义问题。写临时 XML（UTF-16）→ schtasks /Create /XML → 核验。
// RunLevel：管理员模式开启 = Highest（游戏内也可记录，创建需提权环境——
// 管理员模式下 GUI 本身已提权，天然满足）；未提权时自动降级 LeastPrivilege。
// 渲染路径红线：AutostartEnabled 只做文件存在性检查，绝不 spawn 子进程。
#include "autostart.h"
#include "adminmode.h"
#include <windows.h>
#include <shlobj.h>

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunVal = L"KeyboardStats";
const wchar_t* kTaskName = L"KeyboardStats Recorder";
const wchar_t* kTaskFile = L"C://Windows//System32//Tasks//KeyboardStats Recorder";

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
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

std::wstring TempPath(const wchar_t* leaf) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + leaf;
}

bool WriteUtf16File(const std::wstring& path, const std::wstring& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const unsigned short bom = 0xFEFF;
    fwrite(&bom, 2, 1, f);
    fwrite(text.data(), 2, text.size(), f);
    fclose(f);
    return true;
}

std::wstring TaskXml(bool highest) {
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"  <Triggers>\n"
        L"    <LogonTrigger><Enabled>true</Enabled></LogonTrigger>\n"
        L"  </Triggers>\n"
        L"  <Principals>\n"
        L"    <Principal id=\"Author\"><LogonType>InteractiveToken</LogonType><RunLevel>";
    xml += highest ? L"Highest" : L"LeastPrivilege";
    xml += L"</RunLevel></Principal>\n"
        L"  </Principals>\n"
        L"  <Settings>\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        L"    <StartWhenAvailable>true</StartWhenAvailable>\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
        L"    <Enabled>true</Enabled>\n"
        L"  </Settings>\n"
        L"  <Actions Context=\"Author\">\n"
        L"    <Exec><Command>";
    xml += ExePath();
    xml += L"</Command><Arguments>--record</Arguments></Exec>\n"
        L"  </Actions>\n"
        L"</Task>\n";
    return xml;
}

// 旧版注册表方式的清理（迁移）
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
    // 渲染路径安全：纯文件存在性检查，无子进程
    if (GetFileAttributesW(kTaskFile) != INVALID_FILE_ATTRIBUTES) return true;
    // 兼容旧版注册表方式
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(k, kRunVal, nullptr, &type, nullptr, &size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

bool AutostartSet(bool enable) {
    if (!enable) {
        const bool ok = RunHidden(L"schtasks /Delete /F /TN \"KeyboardStats Recorder\"");
        RemoveLegacyRegistry();
        return ok || GetFileAttributesW(kTaskFile) == INVALID_FILE_ATTRIBUTES;
    }
    // ── 注册计划任务 ──
    const bool highest = app::AdminModeFlagged();
    const std::wstring xmlPath = TempPath(L"keyboardstats-task.xml");
    bool ok = false;
    // 先按目标权限写 XML 注册；未提权时 Highest 注册会被拒绝，自动降级重试
    for (int attempt = 0; attempt < 2 && !ok; ++attempt) {
        const bool lvl = attempt == 0 ? highest : false;
        if (!WriteUtf16File(xmlPath, TaskXml(lvl))) break;
        ok = RunHidden(std::wstring(L"schtasks /Create /F /TN \"KeyboardStats Recorder\" /XML \"")
                       + xmlPath + L"\"");
    }
    DeleteFileW(xmlPath.c_str());
    if (!ok) return false;
    // 双重启动防护：迁移时清掉旧注册表 Run 键
    RemoveLegacyRegistry();
    // 任务文件核验
    return GetFileAttributesW(kTaskFile) != INVALID_FILE_ATTRIBUTES;
}
