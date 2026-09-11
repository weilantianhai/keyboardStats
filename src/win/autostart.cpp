// 开机自启动：注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//     KeyboardStats = "<exe 全路径>" --record
//
// 为什么不用计划任务（schtasks）——两个坑叠加，就是"操作失败"的成因：
//   1. /TR 参数里必须内嵌引号（exe 路径带空格），经 CreateProcess 传递后再由
//      schtasks 解析，其转义规则在不同系统上表现不一致，会直接报参数错误；
//   2. /RL HIGHEST（最高权限）在未提权进程里创建任务会被拒绝——于是"管理员开关
//      与自启动开关的时序组合不同"就表现为有时成功、有时失败。
// 现在改成注册表 Run 键：开关打开 = 写键值 → **立即读回逐字节核验**，失败必定
// 被发现并明确提示；关闭 = 删键值 + 顺手清理早期版本留下的计划任务（否则会和
// Run 键同时生效造成双重启动）。
//
// 管理员权限由「管理员模式」开关写入的兼容性标记（AppCompatFlags\Layers 的
// RUNASADMIN）承担：开机由 Run 键启动本 exe 时，Windows 会按该标记自动提权。
//
// 性能红线：**渲染路径（AutostartEnabled，每帧调用）只能是注册表读取**——绝不能
// spawn 子进程（历史上 _wsystem("schtasks /Query") 每帧开一个 cmd.exe，阻塞数百毫秒
// 且闪控制台窗口，表现为设置页卡死 + "奇怪弹窗"）。schtasks 现在只在关闭开关时
// 执行一次用于清残留，且先查任务定义文件，不存在就完全不起子进程。
#include "autostart.h"
#include <windows.h>

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunVal = L"KeyboardStats";
// 早期版本用计划任务实现自启动，其定义文件；存在才值得调 schtasks 去删
const wchar_t* kLegacyTaskName = L"KeyboardStats Recorder";
const wchar_t* kLegacyTaskFile = L"C:\\Windows\\System32\\Tasks\\KeyboardStats Recorder";

// 要写入的自启动命令行（引号包裹 exe，路径含空格也安全）
std::wstring AutostartCommand() { return L"\"" + ExePath() + L"\" --record"; }

// 读当前 Run 值；值不存在/类型不对/空值都返回 false
bool ReadRunValue(std::wstring* out) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(k, kRunVal, nullptr, &type, nullptr, &size);
    if (r != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t)) {
        RegCloseKey(k);
        return false;
    }
    std::wstring buf(size / sizeof(wchar_t), L'\0');
    r = RegQueryValueExW(k, kRunVal, nullptr, &type,
                         reinterpret_cast<BYTE*>(&buf[0]), &size);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS) return false;
    buf.resize(wcslen(buf.c_str()));
    *out = buf;
    return true;
}

// 值是否指向"当前这个 exe"（大小写不敏感地比对开头那一段带引号路径）
bool PointsToThisExe(const std::wstring& value) {
    const std::wstring want = L"\"" + ExePath() + L"\"";
    if (value.size() < want.size()) return false;
    return _wcsnicmp(value.c_str(), want.c_str(), want.size()) == 0;
}

// 隐藏窗口执行命令（清残留用，只调一次）
void RunHidden(const std::wstring& cmd) {
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring c = cmd;
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return;
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

// 清理早期版本的计划任务残留（best-effort）。
// 先查任务定义文件：没有就直接返回，不创建任何子进程。
void RemoveLegacyTask() {
    if (GetFileAttributesW(kLegacyTaskFile) == INVALID_FILE_ATTRIBUTES) return;
    RunHidden(L"schtasks /Delete /F /TN \"" + std::wstring(kLegacyTaskName) + L"\"");
}

} // namespace

std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

bool AutostartEnabled() {
    // 渲染路径安全：一次注册表读取，无子进程
    std::wstring v;
    if (!ReadRunValue(&v)) return false;
    // 值存在但指向别的路径（exe 被搬走、或旧安装留下的）→ 视为未开启：
    // 开关状态才是诚实的，用户打开开关即可覆盖成当前路径。
    return PointsToThisExe(v);
}

bool AutostartSet(bool enable) {
    if (!enable) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, kRunVal);   // 本来就不存在也视为成功
            RegCloseKey(k);
        }
        RemoveLegacyTask();
        return true;
    }

    RemoveLegacyTask();   // 先拆旧机制，避免 Run 键与计划任务同时启动

    const std::wstring cmd = AutostartCommand();
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG w = RegSetValueExW(k, kRunVal, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(cmd.c_str()),
                                  (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    if (w != ERROR_SUCCESS) return false;

    // 写回后立即读回核验：读不到或对不上都算失败，调用方据此给用户明确提示
    // （旧实现只信 schtasks 的 exit code，失败时用户只看到"操作失败"四个字）
    std::wstring back;
    return ReadRunValue(&back) && back == cmd;
}
