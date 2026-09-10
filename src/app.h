#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// UI 全局状态（单线程访问）
struct UiState {
    int page = 0;          // 0=热力图 1=直方图
    bool panelOpen = false;
    int rangeMode = 3;     // 0=今天 1=最近7天 2=最近30天 3=全部 4=自定义
    uint32_t customFrom = 0;   // yyyymmdd
    uint32_t customTo = 0;
};

extern UiState g_ui;
extern HWND g_mainWnd;

std::wstring ExePath();
bool AutostartEnabled();
void AutostartSet(bool enable);
