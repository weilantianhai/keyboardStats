#pragma once
#include <cstdint>
#include <string>

// 本地日期工具。日期一律用整数 yyyymmdd 表示，月份用 yyyymm。
uint32_t TodayLocal();                 // 本地今天，如 20260115
uint32_t NowYm();                      // 本地当前月份，如 202601
int      NowHour();
uint32_t AddDays(uint32_t ymd, int delta);   // delta 可为负
int      DayDiff(uint32_t a, uint32_t b);     // a-b 相差天数
uint32_t YmOf(uint32_t ymd);
std::wstring YmdToStr(uint32_t ymd);   // L"2026-01-15"
std::string FormatEventTimeNow();      // "2026-01-15T09:30:12.345"（本地时间）
