// 开机自启动实现：计划任务（登录触发 + 最高权限）。
// 设计说明见 autostart.h 顶部的五条要点。
#include "autostart.h"
#include "adminmode.h"

#include <windows.h>

namespace {

const wchar_t* kTaskName = L"KeyboardStats Recorder";
const wchar_t* kTaskFile = L"C:\\Windows\\System32\\Tasks\\KeyboardStats Recorder";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunVal = L"KeyboardStats";

// 早期版本用注册表 Run 键实现自启动（登录阶段提权请求会被静默丢弃，已废弃）：
// 开启任务时顺手清理残留值，避免与计划任务形成两个启动点
void RemoveLegacyRunKey() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, kRunVal);
    RegCloseKey(k);
}

// RunLevel 校验结果缓存（渲染路径每帧调用，读文件要节流）
struct TaskStateCache {
    DWORD at = 0;           // GetTickCount 时间戳
    int level = -1;         // -1 未检测 / 0 不存在 / 1 Highest / 2 其它级别
};
TaskStateCache& Cache() {
    static TaskStateCache c;
    return c;
}

constexpr DWORD kCacheMs = 3000;

// ── 任务定义文件解析 ──
// 任务 XML 是 UTF-16LE；读成宽字符后精确匹配完整标签。
// 注意不要用窄字符字面量做 "H\0i\0g..." 匹配——字符串字面量会在第一个 \0 处
// 截断（实际只查找 "H"，用户名里的 H 会造成误判），必须用 wstring。
int ReadTaskLevel() {
    HANDLE h = CreateFileW(kTaskFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    wchar_t buf[4096] = {};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 2, &got, nullptr);
    CloseHandle(h);
    if (got < 4) return 0;
    const std::wstring text(buf, got / sizeof(wchar_t));
    return text.find(L"<RunLevel>Highest</RunLevel>") != std::wstring::npos ? 1 : 2;
}

int TaskLevelCached() {
    auto& c = Cache();
    const DWORD now = GetTickCount();
    if (c.level < 0 || now - c.at > kCacheMs) {
        c.level = ReadTaskLevel();
        c.at = now;
    }
    return c.level;
}

void InvalidateCache() {
    Cache().level = -1;
}

// 隐藏窗口执行命令并等待完成（仅用于开关切换/修复等一次性路径，不在渲染路径）
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

// 任务定义：登录触发 + 最高权限 + 不限时 + 忽略重复实例
std::wstring TaskXml() {
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"  <Triggers>\n"
        L"    <LogonTrigger><Enabled>true</Enabled></LogonTrigger>\n"
        L"  </Triggers>\n"
        L"  <Principals>\n"
        L"    <Principal id=\"Author\">"
        L"<LogonType>InteractiveToken</LogonType><RunLevel>Highest</RunLevel>"
        L"</Principal>\n"
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

// 注册任务（调用方保证已提权）
bool CreateTask() {
    const std::wstring xmlPath = TempPath(L"keyboardstats-task.xml");
    if (!WriteUtf16File(xmlPath, TaskXml())) return false;
    const bool ok = RunHidden(std::wstring(L"schtasks /Create /F /TN \"")
                              + kTaskName + L"\" /XML \"" + xmlPath + L"\"");
    DeleteFileW(xmlPath.c_str());
    InvalidateCache();
    return ok && ReadTaskLevel() == 1;
}

} // namespace

std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

bool AutostartEnabled() {
    return TaskLevelCached() == 1;   // 必须存在且是 Highest，否则视为未开启
}

AutostartResult AutostartEnable() {
    // 已是最高权限任务：直接成功（幂等）
    if (ReadTaskLevel() == 1) {
        InvalidateCache();
        return AutostartResult::Ok;
    }
    if (!app::RunningElevated()) return AutostartResult::NeedElevation;
    const bool ok = CreateTask();
    if (ok) RemoveLegacyRunKey();
    return ok ? AutostartResult::Ok : AutostartResult::Failed;
}

bool AutostartDisable() {
    RunHidden(std::wstring(L"schtasks /Delete /F /TN \"") + kTaskName + L"\"");
    InvalidateCache();
    return ReadTaskLevel() == 0;
}

void AutostartRepairIfNeeded() {
    // 存量任务权限级别不对（或不存在）时重建为 Highest；未提权则什么也不做
    if (!app::RunningElevated()) return;
    if (ReadTaskLevel() == 1) return;
    // 仅当任务存在（用户开过自启动）时才自动修复，不擅自替用户开启
    if (ReadTaskLevel() == 2) CreateTask();
}
