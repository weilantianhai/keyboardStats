#include "gui.h"
#include "app.h"
#include "storage.h"
#include "layout.h"
#include "timeutil.h"
#include "hook.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#pragma GCC diagnostic ignored "-Wcast-function-type"

// ────────────────────────── 全局 ──────────────────────────

UiState g_ui;
HWND g_mainWnd = nullptr;

#define WM_TRAY   (WM_APP + 1)
#define ID_TRAY   1
#define IDT_FLUSH 1

// 控件
enum {
    IDC_BTN_PANEL = 101, IDC_BTN_HEAT, IDC_BTN_HIST,
    IDC_BTN_TODAY, IDC_BTN_7, IDC_BTN_30, IDC_BTN_ALL,
    IDC_DTP_FROM, IDC_DTP_TO, IDC_BTN_APPLY,
};

static HWND s_btnPanel, s_btnHeat, s_btnHist;
static HWND s_btnToday, s_btn7, s_btn30, s_btnAll;
static HWND s_dtpFrom, s_dtpTo, s_btnApply;

static HFONT s_font, s_fontSmall, s_fontKey, s_fontCount;
static NOTIFYICONDATAW g_nid = {};

// 客户区尺寸（固定大小窗口）
static const int CW_W = 916, CW_H = 640;
static const int MARGIN = 16;

// ────────────────────────── 通用小函数 ──────────────────────────

static std::wstring FmtCount(long n) {
    wchar_t buf[32];
    if (n >= 1000000) swprintf(buf, 32, L"%.1fM", n / 1000000.0);
    else if (n >= 100000) swprintf(buf, 32, L"%ldk", n / 1000);
    else swprintf(buf, 32, L"%ld", n);
    return buf;
}

static void Check(HWND h, bool on) {
    std::wstring t; wchar_t buf[64];
    GetWindowTextW(h, buf, 64);
    t = buf;
    bool was = t.rfind(L"\x2713 ", 0) == 0;                 // "✓ " 前缀
    std::wstring bare = was ? t.substr(2) : t;
    SetWindowTextW(h, (std::wstring(on ? L"\x2713 " : L"") + bare).c_str());
}

// ────────────────────────── 热度颜色（蓝→黄→红） ──────────────────────────

static COLORREF HeatColor(double t) {
    if (t <= 0) return RGB(234, 238, 244);                 // 无按键：浅灰
    struct Stop { double t; COLORREF c; };
    static const Stop stops[] = {
        { 0.0, RGB(49, 54, 149) }, { 0.5, RGB(240, 220, 120) }, { 1.0, RGB(178, 24, 43) },
    };
    for (int i = 0; i < 2; ++i) {
        if (t <= stops[i + 1].t) {
            double k = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
            auto lerp = [&](int a, int b) { return (int)(a + (b - a) * k); };
            return RGB(lerp(GetRValue(stops[i].c), GetRValue(stops[i + 1].c)),
                       lerp(GetGValue(stops[i].c), GetGValue(stops[i + 1].c)),
                       lerp(GetBValue(stops[i].c), GetBValue(stops[i + 1].c)));
        }
    }
    return stops[2].c;
}

// ────────────────────────── 时间段应用 ──────────────────────────

static uint32_t DtpValue(HWND h) {
    SYSTEMTIME st{};
    if (!SendMessageW(h, DTM_GETSYSTEMTIME, 0, (LPARAM)&st)) return 0;
    return (uint32_t)(st.wYear * 10000 + st.wMonth * 100 + st.wDay);
}

static void RefreshData() { InvalidateRect(g_mainWnd, nullptr, FALSE); }

static void SetRangeMode(int mode) {
    g_ui.rangeMode = mode;
    Check(s_btnToday, mode == 0); Check(s_btn7, mode == 1);
    Check(s_btn30, mode == 2);   Check(s_btnAll, mode == 3);
    RefreshData();
}

static std::wstring RangeTitle() {
    uint32_t today = TodayLocal();
    switch (g_ui.rangeMode) {
        case 0: return L"时间段：今天 (" + YmdToStr(today) + L")";
        case 1: return L"时间段：最近 7 天 (" + YmdToStr(AddDays(today, -6)) + L" ~ " + YmdToStr(today) + L")";
        case 2: return L"时间段：最近 30 天 (" + YmdToStr(AddDays(today, -29)) + L" ~ " + YmdToStr(today) + L")";
        case 3: return L"时间段：全部";
        default: {
            uint32_t a = g_ui.customFrom, b = g_ui.customTo;
            if (!a || !b) return L"时间段：全部";
            if (a > b) std::swap(a, b);
            return L"时间段：自定义 " + YmdToStr(a) + L" ~ " + YmdToStr(b);
        }
    }
}

// ────────────────────────── 开机自启（注册表 HKCU Run） ──────────────────────────

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunVal = L"KeyboardStats";

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

// ────────────────────────── 托盘 ──────────────────────────

static void ShowTrayMenu(HWND hwnd) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 2001, L"打开热力图");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), 2002, L"开机自启动");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 2003, L"退出");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}

static void ShowMain() {
    ShowWindow(g_mainWnd, SW_RESTORE);
    SetForegroundWindow(g_mainWnd);
    RefreshData();
}

// ────────────────────────── 绘制：热力图页 ──────────────────────────

static void DrawKeyboard(HDC dc, int top, const RangeStats& rs) {
    const float maxX = 23.5f + 1.0f;                        // 布局总宽（单位 u）
    const int u = (CW_W - 2 * MARGIN) / (int)maxX;
    long maxC = 1;
    for (long c : rs.counts) if (c > maxC) maxC = c;

    HBRUSH bgKey = CreateSolidBrush(RGB(234, 238, 244));
    HPEN penKey = CreatePen(PS_SOLID, 1, RGB(160, 168, 180));
    HGDIOBJ oldPen = SelectObject(dc, penKey);
    SetBkMode(dc, TRANSPARENT);

    for (const KeyDef& k : kKeys) {
        long c = rs.counts[k.vk];
        HBRUSH br = CreateSolidBrush(HeatColor(maxC > 1 ? (double)c / maxC : 0));
        RECT r{ (int)(MARGIN + k.x * u) + 1, top + (int)(k.y * u) + 1,
                (int)(MARGIN + (k.x + k.w) * u) - 1, top + (int)((k.y + k.h) * u) - 1 };
        FillRect(dc, &r, br);
        DeleteObject(br);
        FrameRect(dc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
        if (k.cap) {
            SelectObject(dc, s_fontKey);
            SetTextColor(dc, c > 0 ? RGB(250, 250, 250) : RGB(70, 76, 88));
            RECT rc = r; rc.bottom = r.top + (r.bottom - r.top) * 2 / 3;
            DrawTextW(dc, k.cap, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (c > 0) {
            SelectObject(dc, s_fontCount);
            SetTextColor(dc, c > 0 ? RGB(255, 235, 235) : RGB(70, 76, 88));
            std::wstring num = FmtCount(c);
            RECT rc = r; rc.top = r.top + (r.bottom - r.top) / 3;
            DrawTextW(dc, num.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(penKey); DeleteObject(bgKey);

    // 图例
    int ly = top + 6 * u + 8;
    SelectObject(dc, s_fontSmall);
    SetTextColor(dc, RGB(70, 76, 88));
    TextOutW(dc, MARGIN, ly, L"少", 1);
    for (int i = 0; i < 8; ++i) {
        RECT r{ MARGIN + 24 + i * 18, ly, MARGIN + 24 + (i + 1) * 18, ly + 12 };
        HBRUSH br = CreateSolidBrush(HeatColor((i + 0.5) / 8.0));
        FillRect(dc, &r, br); DeleteObject(br);
    }
    TextOutW(dc, MARGIN + 24 + 8 * 18 + 6, ly, L"多", 1);
}

static void DrawTop10(HDC dc, int y, const RangeStats& rs) {
    std::vector<std::pair<long, uint8_t>> top;
    for (int vk = 0; vk < 256; ++vk)
        if (rs.counts[vk] > 0 && StatName((uint8_t)vk)) top.push_back({ rs.counts[vk], (uint8_t)vk });
    std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.first > b.first; });

    wchar_t line[512] = L"";
    std::wstring s = L"Top 10：";
    for (size_t i = 0; i < top.size() && i < 10; ++i)
        s += (i ? L"  ·  " : L"") + std::wstring(StatName(top[i].second)) + L" " + FmtCount(top[i].first);
    if (top.empty()) s += L"（该时段暂无按键记录）";
    wcscpy_s(line, s.c_str());

    SetTextColor(dc, RGB(40, 44, 54));
    RECT rc{ MARGIN, y, CW_W - MARGIN, y + 24 };
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawTextW(dc, line, -1, &rc, DT_SINGLELINE | DT_VCENTER);
}

// ────────────────────────── 绘制：直方图页 ──────────────────────────

static void DrawHistogram(HDC dc, int top, int bottom, const RangeStats& rs) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%ls 按键总量：%s", RangeTitle().c_str(), FmtCount(rs.total).c_str());
    SelectObject(dc, s_font);
    SetTextColor(dc, RGB(40, 44, 54));
    TextOutW(dc, MARGIN, top, buf, (int)wcslen(buf));

    const auto& bs = rs.buckets;
    if (bs.empty() || rs.total == 0) {
        SetTextColor(dc, RGB(120, 126, 138));
        TextOutW(dc, MARGIN, top + 30, L"该时段暂无数据", 7);
        return;
    }

    const int gx = MARGIN, gw = CW_W - 2 * MARGIN;
    const int gy = top + 34, gh = bottom - gy - 26;
    long maxV = 1;
    for (auto& b : bs) if (b.count > maxV) maxV = b.count;

    // 横向网格线
    SelectObject(dc, s_fontSmall);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(BLACK_PEN));
    for (int g = 1; g <= 4; ++g) {
        int yy = gy + gh - gh * g / 4;
        MoveToEx(dc, gx, yy, nullptr); LineTo(dc, gx + gw, yy);
        swprintf(buf, 64, L"%ld", maxV * g / 4);
        SetTextColor(dc, RGB(120, 126, 138));
        TextOutW(dc, gx, yy - 14, buf, (int)wcslen(buf));
    }
    SelectObject(dc, oldPen);

    HBRUSH bar = CreateSolidBrush(RGB(70, 116, 194));
    int step = (int)bs.size() > 0 ? gw / (int)bs.size() : gw;
    int labelEvery = bs.size() > 32 ? (int)((bs.size() + 15) / 16) : 1;
    for (size_t i = 0; i < bs.size(); ++i) {
        int h = bs[i].count ? std::max(2, (int)((int64_t)gh * bs[i].count / maxV)) : 1;
        int x = gx + (int)i * step;
        RECT r{ x + 1, gy + gh - h, x + step - (step > 3 ? 1 : 0), gy + gh };
        FillRect(dc, &r, bar);
        if ((int)i % labelEvery == 0) {
            SetTextColor(dc, RGB(90, 96, 108));
            TextOutW(dc, x, gy + gh + 4, bs[i].label.c_str(), (int)bs[i].label.size());
        }
    }
    DeleteObject(bar);
}

// ────────────────────────── 窗口过程 ──────────────────────────

static void LayoutControls() {
    // 顶行按钮
    SetWindowPos(s_btnPanel, nullptr, MARGIN, 12, 110, 28, SWP_NOZORDER);
    SetWindowPos(s_btnHeat,  nullptr, MARGIN + 120, 12, 90, 28, SWP_NOZORDER);
    SetWindowPos(s_btnHist,  nullptr, MARGIN + 216, 12, 90, 28, SWP_NOZORDER);
    bool p = g_ui.panelOpen;
    int py = 48, ph = p ? 96 : 0;
    // 时间面板内部
    SetWindowPos(s_btnToday, nullptr, MARGIN + 10, py + 8, 74, 28, SWP_NOZORDER);
    SetWindowPos(s_btn7,     nullptr, MARGIN + 92, py + 8, 104, 28, SWP_NOZORDER);
    SetWindowPos(s_btn30,    nullptr, MARGIN + 204, py + 8, 104, 28, SWP_NOZORDER);
    SetWindowPos(s_btnAll,   nullptr, MARGIN + 316, py + 8, 74, 28, SWP_NOZORDER);
    SetWindowPos(s_dtpFrom,  nullptr, MARGIN + 404, py + 8, 130, 28, SWP_NOZORDER);
    SetWindowPos(s_dtpTo,    nullptr, MARGIN + 556, py + 8, 130, 28, SWP_NOZORDER);
    SetWindowPos(s_btnApply, nullptr, MARGIN + 700, py + 8, 90, 28, SWP_NOZORDER);
    for (HWND h : { s_btnToday, s_btn7, s_btn30, s_btnAll, s_dtpFrom, s_dtpTo, s_btnApply })
        ShowWindow(h, p ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_mainWnd, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_mainWnd = hwnd;
        HINSTANCE hi = ((LPCREATESTRUCTW)lp)->hInstance;
        s_btnPanel = CreateWindowExW(0, L"BUTTON", L"▼ 时间段", WS_CHILD | WS_VISIBLE,
                                     0, 0, 110, 28, hwnd, (HMENU)IDC_BTN_PANEL, hi, nullptr);
        s_btnHeat = CreateWindowExW(0, L"BUTTON", L"✓ 热力图", WS_CHILD | WS_VISIBLE,
                                     0, 0, 90, 28, hwnd, (HMENU)IDC_BTN_HEAT, hi, nullptr);
        s_btnHist = CreateWindowExW(0, L"BUTTON", L"直方图", WS_CHILD | WS_VISIBLE,
                                     0, 0, 90, 28, hwnd, (HMENU)IDC_BTN_HIST, hi, nullptr);
        s_btnToday = CreateWindowExW(0, L"BUTTON", L"今天", WS_CHILD,
                                     0, 0, 74, 28, hwnd, (HMENU)IDC_BTN_TODAY, hi, nullptr);
        s_btn7 = CreateWindowExW(0, L"BUTTON", L"最近 7 天", WS_CHILD,
                                     0, 0, 104, 28, hwnd, (HMENU)IDC_BTN_7, hi, nullptr);
        s_btn30 = CreateWindowExW(0, L"BUTTON", L"最近 30 天", WS_CHILD,
                                     0, 0, 104, 28, hwnd, (HMENU)IDC_BTN_30, hi, nullptr);
        s_btnAll = CreateWindowExW(0, L"BUTTON", L"全部", WS_CHILD,
                                     0, 0, 74, 28, hwnd, (HMENU)IDC_BTN_ALL, hi, nullptr);
        s_dtpFrom = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | DTS_SHORTDATECENTURYFORMAT,
                                     0, 0, 130, 28, hwnd, (HMENU)IDC_DTP_FROM, hi, nullptr);
        s_dtpTo = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | DTS_SHORTDATECENTURYFORMAT,
                                     0, 0, 130, 28, hwnd, (HMENU)IDC_DTP_TO, hi, nullptr);
        s_btnApply = CreateWindowExW(0, L"BUTTON", L"应用", WS_CHILD,
                                     0, 0, 90, 28, hwnd, (HMENU)IDC_BTN_APPLY, hi, nullptr);
        SendMessageW(s_dtpFrom, DTM_SETFORMATW, 0, (LPARAM)L"yyyy'-'MM'-'dd");
        SendMessageW(s_dtpTo, DTM_SETFORMATW, 0, (LPARAM)L"yyyy'-'MM'-'dd");

        for (HWND h : { s_btnPanel, s_btnHeat, s_btnHist, s_btnToday, s_btn7, s_btn30, s_btnAll, s_btnApply })
            SendMessageW(h, WM_SETFONT, (WPARAM)s_font, TRUE);
        LayoutControls();
        SetRangeMode(3);                                   // 默认：全部
        SetTimer(hwnd, IDT_FLUSH, 1000, nullptr);
        return 0;
    }
    case WM_TIMER:
        StorageFlushIfDue();
        if (!IsIconic(hwnd) && IsWindowVisible(hwnd)) RefreshData();
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONDBLCLK) ShowMain();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_BTN_PANEL) { g_ui.panelOpen = !g_ui.panelOpen; SetWindowTextW(s_btnPanel, g_ui.panelOpen ? L"▲ 时间段" : L"▼ 时间段"); LayoutControls(); }
        else if (id == IDC_BTN_HEAT) { g_ui.page = 0; Check(s_btnHeat, true); Check(s_btnHist, false); RefreshData(); }
        else if (id == IDC_BTN_HIST) { g_ui.page = 1; Check(s_btnHeat, false); Check(s_btnHist, true); RefreshData(); }
        else if (id == IDC_BTN_TODAY) SetRangeMode(0);
        else if (id == IDC_BTN_7)     SetRangeMode(1);
        else if (id == IDC_BTN_30)    SetRangeMode(2);
        else if (id == IDC_BTN_ALL)   SetRangeMode(3);
        else if (id == IDC_BTN_APPLY) {
            g_ui.customFrom = DtpValue(s_dtpFrom);
            g_ui.customTo = DtpValue(s_dtpTo);
            SetRangeMode(4);
        }
        else if (id == 2001) ShowMain();
        else if (id == 2002) { AutostartSet(!AutostartEnabled()); }
        else if (id == 2003) DestroyWindow(hwnd);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT full{ 0, 0, CW_W, CW_H };
        FillRect(dc, &full, (HBRUSH)GetStockObject(WHITE_BRUSH));

        RangeStats rs = QueryRange(g_ui.rangeMode, g_ui.customFrom, g_ui.customTo);
        wchar_t total[64];
        swprintf(total, 64, L"%ls   按键总数：%s", RangeTitle().c_str(), FmtCount(rs.total).c_str());
        SelectObject(dc, s_font);
        SetTextColor(dc, RGB(40, 44, 54));
        TextOutW(dc, MARGIN, 44 + (g_ui.panelOpen ? 100 : 0), total, (int)wcslen(total));

        int contentTop = 70 + (g_ui.panelOpen ? 100 : 0);
        if (g_ui.page == 0) {
            DrawKeyboard(dc, contentTop, rs);
            DrawTop10(dc, contentTop + 6 * ((CW_W - 2 * MARGIN) / 24) + 30, rs);
        } else {
            DrawHistogram(dc, contentTop, CW_H - 40, rs);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);                         // 关窗 = 藏进托盘
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_FLUSH);
        RemoveHook();
        StorageFlushNow();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ────────────────────────── 入口 ──────────────────────────

int GuiRun(bool startHidden) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_DATE_CLASSES };
    InitCommonControlsEx(&icc);

    s_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    s_fontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    s_fontKey = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, GB2312_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    s_fontCount = CreateFontW(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"KeyboardStatsWnd";
    RegisterClassW(&wc);

    RECT r{ 0, 0, CW_W, CW_H };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_mainWnd = CreateWindowExW(0, wc.lpszClassName, L"KeyboardStats 键盘热力统计",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);

    // 托盘图标（用系统默认图标，免资源文件）
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_mainWnd;
    g_nid.uID = ID_TRAY;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"KeyboardStats 键盘热力统计");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    InstallHook();                                         // 键盘钩子
    ShowWindow(g_mainWnd, startHidden ? SW_HIDE : SW_SHOWNORMAL);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
