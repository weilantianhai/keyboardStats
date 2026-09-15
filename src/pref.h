#pragma once
// 偏好文件读写：%APPDATA%\KeyboardStats 下的 key=value 纯文本（可用 KEYBOARDSTATS_DIR 覆盖）
// 刻意不依赖 EUI-NEO：数据层（storage）与 UI 层共用，独立测试程序也能直接链接。
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace app {

// 设置目录（偏好文件所在；与"数据文件夹"可以不同）
inline std::wstring PrefDirPath() {
    wchar_t custom[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"KEYBOARDSTATS_DIR", custom, MAX_PATH) > 0)
        return std::wstring(custom);

    // APPDATA 环境变量在某些启动方式下会是空的，直接拼就会退化到 "\KeyboardStats"（盘根目录），
    // 那相当于偏好跑到别人家去了。所以空了就改用系统 API 取漫游 AppData，再不行退到 exe 同级。
    wchar_t appdata[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0 && appdata[0] != L'\0')
        return std::wstring(appdata) + L"\\KeyboardStats";
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)) && appdata[0] != L'\0')
        return std::wstring(appdata) + L"\\KeyboardStats";

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    const size_t cut = dir.find_last_of(L"\\/");
    return (cut == std::wstring::npos ? dir : dir.substr(0, cut)) + L"\\KeyboardStats.config";
}

inline std::wstring PrefFilePath(const wchar_t* name) {
    return PrefDirPath() + L"\\" + name;
}

inline std::string PrefGetValue(const wchar_t* file, const char* key, const char* fallback) {
    FILE* f = _wfopen(PrefFilePath(file).c_str(), L"rb");
    if (!f) return fallback;
    std::string all;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);

    const std::string k = std::string(key) + "=";
    size_t pos = all.find(k);
    if (pos == std::string::npos) return fallback;
    pos += k.size();
    size_t end = all.find_first_of("\r\n", pos);
    return all.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

inline void PrefSetValue(const wchar_t* file, const char* key, const char* value) {
    const std::wstring path = PrefFilePath(file);
    std::map<std::string, std::string> kv;
    std::vector<std::string> order;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        std::string all;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
        fclose(f);
        size_t p = 0;
        while (p < all.size()) {
            size_t e = all.find_first_of("\r\n", p);
            if (e == std::string::npos) e = all.size();
            std::string line = all.substr(p, e - p);
            p = e + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            if (!kv.count(k)) order.push_back(k);
            kv[k] = line.substr(eq + 1);
        }
    }
    if (!kv.count(key)) order.push_back(key);
    kv[key] = value;

    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    for (const std::string& k : order) fputs((k + "=" + kv[k] + "\n").c_str(), f);
    fclose(f);
}

// 编码转换（偏好值是 UTF-8 文本，路径用 UTF-16）
inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace app
