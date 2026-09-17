// 开机自启动实现：计划任务（登录触发 + 最高权限）。
// 设计说明见 autostart.h 顶部的五条要点。
#include "autostart.h"
#include "adminmode.h"

#include <windows.h>
#include <sddl.h>
#include <vector>
#include <string>

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
    // 任务计划程序实际写出的值可能是 Highest 或 HighestAvailable（两者都表示最高权限，
    // 后者是 schtasks /RL HIGHEST 的产物）——按前缀匹配，两者都算最高权限
    return text.find(L"<RunLevel>Highest") != std::wstring::npos ? 1 : 2;
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

// 最近一次隐藏命令的退出码与输出（诊断用：创建失败时写入错误日志）
DWORD g_lastRunCode = 1;
std::string g_lastRunOutput;

// 隐藏窗口执行命令并等待完成（仅用于开关切换/修复等一次性路径，不在渲染路径）；
// 捕获 stdout/stderr 供失败诊断（schtasks 的错误信息以 ANSI 输出）
bool RunHidden(const std::wstring& cmd) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi = {};
    std::wstring c = cmd;
    g_lastRunOutput.clear();
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return false;
    }
    CloseHandle(wr);
    char buf[1024];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got > 0)
        g_lastRunOutput.append(buf, got);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    g_lastRunCode = code;
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

// 当前用户 SID（形如 S-1-5-21-...）：Highest 任务的 Principal 需要明确的用户身份，
// 不指定时部分系统上 schtasks /XML 会以 exit 1 拒绝（且不输出细节）
std::wstring CurrentUserSid() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return L"";
    DWORD len = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    std::wstring sid;
    if (len > 0) {
        std::vector<BYTE> buf(len);
        if (GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
            LPWSTR str = nullptr;
            if (ConvertSidToStringSidW(((TOKEN_USER*)buf.data())->User.Sid, &str)) {
                sid = str;
                LocalFree(str);
            }
        }
    }
    CloseHandle(tok);
    return sid;
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
        L"    <Principal id=\"Author\"><UserId>";
    xml += CurrentUserSid();
    xml += L"</UserId>"
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

// 失败诊断日志：exit code + 环境信息（设置目录 autostart-error.txt）
void WriteErrorLog(const wchar_t* stage) {
    wchar_t dir[MAX_PATH] = {};
    // 设置目录与 storage 的 StorageSettingsDir 一致：%APPDATA%\KeyboardStats
    if (!GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) return;
    const std::wstring path = std::wstring(dir) + L"\\KeyboardStats\\autostart-error.txt";
    CreateDirectoryW((std::wstring(dir) + L"\\KeyboardStats").c_str(), nullptr);
    FILE* f = nullptr;
    // 追加模式：保留多次尝试的完整线索（XML 失败原因 + 兜底结果）
    if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f,
            "[%04u-%02u-%02u %02u:%02u:%02u] stage=%ls\n"
            "schtasks exit code = 0x%08lX\n"
            "elevated = %d\n"
            "task file exists = %d\n"
            "exe = %ls\n"
            "cmd = schtasks /Create /F /TN \"%ls\" /XML <temp>\n",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, stage,
            (unsigned long)g_lastRunCode,
            app::RunningElevated() ? 1 : 0,
            GetFileAttributesW(kTaskFile) != INVALID_FILE_ATTRIBUTES ? 1 : 0,
            ExePath().c_str(), kTaskName);
    if (!g_lastRunOutput.empty()) {
        fprintf(f, "--- schtasks output ---\n%.*s\n-----------------------\n",
                (int)g_lastRunOutput.size(), g_lastRunOutput.c_str());
    }
    fclose(f);
}

// 注册任务（调用方保证已提权）。两个方案依次尝试：
//  ① 命令行 /TR：已实测成功（路径无空格时 /TR 值整体一个引号、内部无需嵌套引号，
//     早年"内嵌引号转义"坑不存在）；schtasks /RL HIGHEST 写出 HighestAvailable
//  ② XML 注册：兜底（路径含空格时用），Command/Arguments 无转义问题但部分系统
//     上以 exit 1 拒绝（原因不可见），仅在 ① 失败后尝试
bool CreateTask() {
    // 方案 ①：命令行（无空格路径）
    const std::wstring exe = ExePath();
    if (exe.find(L' ') == std::wstring::npos) {
        const std::wstring cmd = std::wstring(L"schtasks /Create /F /TN \"") + kTaskName
                                 + L"\" /TR \"" + exe + L" --record\" /SC ONLOGON /RL HIGHEST";
        const bool ok = RunHidden(cmd);
        InvalidateCache();
        if (ok && ReadTaskLevel() == 1) return true;
        WriteErrorLog(L"cmdline-create");
    }
    // 方案 ②：XML 兜底
    {
        const std::wstring xmlPath = TempPath(L"keyboardstats-task.xml");
        if (WriteUtf16File(xmlPath, TaskXml())) {
            const bool ok = RunHidden(std::wstring(L"schtasks /Create /F /TN \"")
                                      + kTaskName + L"\" /XML \"" + xmlPath + L"\"");
            DeleteFileW(xmlPath.c_str());
            InvalidateCache();
            if (ok && ReadTaskLevel() == 1) return true;
            WriteErrorLog(L"xml-create");
        } else {
            WriteErrorLog(L"write-xml");
        }
    }
    return false;
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
