#pragma once
// 通用小工具：颜色/编码/格式化。header-only。
#include "eui_neo.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace app {

inline core::Color Hex(unsigned rgb, float a = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f,
            ((rgb >> 8) & 0xFF) / 255.0f,
            (rgb & 0xFF) / 255.0f, a};
}

inline std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::string WithCommas(long v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", v);
    std::string s = buf;
    for (int pos = (int)s.size() - 3; pos > 0; pos -= 3) s.insert(pos, ",");
    return s;
}

inline uint32_t YmdOf(int y, int m, int d) {
    return (uint32_t)(y * 10000 + m * 100 + d);
}

} // namespace app
