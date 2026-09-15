#pragma once
// Win32 文件选择对话框（导入/导出用；框架无文件对话框组件）
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <string>

namespace app {

// 选择文件夹（SHBrowseForFolder；框架无目录选择组件）
inline bool PickFolder(const wchar_t* title, std::wstring* out) {
    wchar_t display[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = GetActiveWindow();
    bi.pszDisplayName = display;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH] = {};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok || path[0] == L'\0') return false;
    *out = path;
    return true;
}

// filter 形如 L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0"
// initialDir 非空时对话框从这里打开（不传就沿用上次的目录，可能很意外）
inline bool PickOpenFile(const wchar_t* title, const wchar_t* filter, std::wstring* out,
                         const std::wstring& initialDir = std::wstring()) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    if (!initialDir.empty()) ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    *out = buf;
    return true;
}

inline bool PickSaveFile(const wchar_t* title, const wchar_t* filter,
                         const wchar_t* defaultExt, const wchar_t* defaultName,
                         std::wstring* out,
                         const std::wstring& initialDir = std::wstring()) {
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
    if (!initialDir.empty()) ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    *out = buf;
    return true;
}

} // namespace app
