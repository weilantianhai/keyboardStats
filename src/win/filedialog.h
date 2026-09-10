#pragma once
// Win32 文件选择对话框（导入/导出用；框架无文件对话框组件）
#include <windows.h>
#include <commdlg.h>

#include <string>

namespace app {

// filter 形如 L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0"
inline bool PickOpenFile(const wchar_t* title, const wchar_t* filter, std::wstring* out) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    *out = buf;
    return true;
}

inline bool PickSaveFile(const wchar_t* title, const wchar_t* filter,
                         const wchar_t* defaultExt, const wchar_t* defaultName,
                         std::wstring* out) {
    wchar_t buf[MAX_PATH] = {};
    if (defaultName != nullptr) wcsncpy(buf, defaultName, MAX_PATH - 1);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    *out = buf;
    return true;
}

} // namespace app
