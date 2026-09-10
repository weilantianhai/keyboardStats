// 无界面记录进程：不初始化 OpenGL/字体/任何框架，只有钩子 + 计数 + 托盘。
// 由 app.cpp 在静态初始化阶段（早于框架 main）分流进入，消息循环常驻，内存占用极小。
#include "recorder.h"
#include "hook.h"
#include "storage.h"
#include "pref.h"

#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <string>

namespace app {

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;   // 托盘回调消息
constexpr int  IDM_SHOW = 1;
constexpr int  IDM_EXIT = 2;

HWND s_wnd = nullptr;
NOTIFYICONDATAW s_nid = {};
UINT s_taskbarCreated = 0;
HICON s_icon = nullptr;

// exe 同级 assets\icon.png（与 GUI 窗口图标同源）。GDI+ 只在取图标瞬间使用后关闭。
static std::wstring AssetIconPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    const size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos ? p : p.substr(0, s + 1)) + L"assets\\icon.png";
}

static HICON LoadAppIcon() {
    namespace Gdi = Gdiplus;
    ULONG_PTR token = 0;
    Gdi::GdiplusStartupInput in;
    if (Gdi::GdiplusStartup(&token, &in, nullptr) == Gdi::Ok) {
        Gdi::GpBitmap* bmp = nullptr;
        if (Gdi::DllExports::GdipCreateBitmapFromFile(AssetIconPath().c_str(), &bmp) == Gdi::Ok && bmp) {
            HICON raw = nullptr;
            if (Gdi::DllExports::GdipCreateHICONFromBitmap(bmp, &raw) == Gdi::Ok && raw) {
                // 复制一份，随后就可以关掉 GDI+
                HICON own = static_cast<HICON>(CopyImage(raw, IMAGE_ICON, 0, 0, 0));
                DestroyIcon(raw);
                Gdi::DllExports::GdipDisposeImage(bmp);
                Gdi::GdiplusShutdown(token);
                if (own) return own;
            } else {
                Gdi::DllExports::GdipDisposeImage(bmp);
                Gdi::GdiplusShutdown(token);
            }
        } else {
            Gdi::GdiplusShutdown(token);
        }
    }
    return LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);   // 兜底：系统图标
}

static void TrayAdd() {
    ZeroMemory(&s_nid, sizeof s_nid);
    s_nid.cbSize = sizeof s_nid;
    s_nid.hWnd = s_wnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_TRAY;
    s_nid.hIcon = s_icon;
    lstrcpynW(s_nid.szTip, L"KeyboardStats 记录中（点右键操作）", ARRAYSIZE(s_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &s_nid);
}

// 唤起已运行的 GUI 主窗口；没有就启动一个（同 exe 不带参数）
static std::wstring ExeDirForGui() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    const size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : p.substr(0, s);
}

static void ShowGui() {
    static const wchar_t* kTitles[] = {
        L"KeyboardStats 键盘热力统计",
        nullptr,
    };
    for (int i = 0; kTitles[i]; ++i) {
        if (HWND h = FindWindowW(nullptr, kTitles[i])) {
            ShowWindow(h, IsIconic(h) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(h);
            return;
        }
    }
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    std::wstring dir = ExeDirForGui();
    if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void ShutdownRecorder() {
    RemoveHook();
    StorageFlushNow();
    Shell_NotifyIconW(NIM_DELETE, &s_nid);
    PostQuitMessage(0);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == s_taskbarCreated) {   // Explorer 重启后托盘图标要重挂
        TrayAdd();
        return 0;
    }
    switch (msg) {
        case WM_TRAY:
            if (lp == WM_LBUTTONDBLCLK) {
                ShowGui();
            } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, IDM_SHOW, L"打开 KeyboardStats");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, IDM_EXIT, L"退出（停止记录）");
                SetForegroundWindow(h);   // 让菜单在点击外部时自动收起
                const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY,
                                               pt.x, pt.y, 0, h, nullptr);
                DestroyMenu(m);
                if (cmd == IDM_SHOW) ShowGui();
                if (cmd == IDM_EXIT) PostMessageW(h, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_TIMER:
            // wp=1：落盘节流（storage 内部 5 秒一写）；wp=2：GUI 侧控制事件
            if (wp == 1) {
                StorageFlushIfDue();
            } else if (wp == 2) {
                HANDLE evs[2] = {
                    OpenEventW(SYNCHRONIZE, FALSE, kIpcReload),
                    OpenEventW(SYNCHRONIZE, FALSE, kIpcShutdown),
                };
                if (evs[0]) {
                    if (WaitForSingleObject(evs[0], 0) == WAIT_OBJECT_0) {
                        ResetEvent(evs[0]);
                        StorageReloadFull();   // GUI 清除/切换了数据，跟着重置
                    }
                    CloseHandle(evs[0]);
                }
                if (evs[1]) {
                    if (WaitForSingleObject(evs[1], 0) == WAIT_OBJECT_0) {
                        ResetEvent(evs[1]);
                        CloseHandle(evs[1]);
                        ShutdownRecorder();
                    } else {
                        CloseHandle(evs[1]);
                    }
                }
            }
            return 0;

        case WM_CLOSE:   // 托盘菜单「退出」
            ShutdownRecorder();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

[[noreturn]] void RecorderRun() {
    // 单实例：已有一个记录进程在跑就直接退（GUI 拉起时可能赛跑）
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kIpcRecorderMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) ExitProcess(0);

    StorageInit();
    InstallHook();
    atexit([] {
        RemoveHook();
        StorageFlushNow();
    });

    // 隐藏的普通窗口（非 message-only）：承载托盘回调、定时器和菜单的 foreground 语义
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KeyboardStatsRecorderWnd";
    RegisterClassW(&wc);
    s_wnd = CreateWindowExW(0, wc.lpszClassName, L"KeyboardStats Recorder", 0,
                            0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);

    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    s_icon = LoadAppIcon();
    TrayAdd();

    SetTimer(s_wnd, 1, 500, nullptr);   // 落盘节流
    SetTimer(s_wnd, 2, 300, nullptr);   // 控制事件轮询

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ExitProcess(0);
}

bool EnsureRecorderRunning() {
    // 已经在跑？
    if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kIpcRecorderMutex)) {
        CloseHandle(m);
        return true;
    }
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --record";
    std::wstring dir = ExeDirForGui();
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr,
                                   dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (!ok) return false;
    // 等记录进程挂上互斥（最多 ~2 秒）
    for (int i = 0; i < 20; ++i) {
        Sleep(100);
        if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kIpcRecorderMutex)) {
            CloseHandle(m);
            return true;
        }
    }
    return false;
}

void RecorderShowGui() { ShowGui(); }

} // namespace app
