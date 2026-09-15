#include "timeutil.h"
#include <windows.h>
#include <cstdio>

// Howard Hinnant 的公历日期算法：纯日历算术，无时区/夏令时问题
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);            // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int)doe - 719468;
}

static void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
    z += 719468;
    const int era = (int)((z >= 0 ? z : z - 146096) / 146097);
    const unsigned doe = (unsigned)(z - (int64_t)era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (*m <= 2));
}

static void SplitYmd(uint32_t ymd, int* y, unsigned* m, unsigned* d) {
    *y = ymd / 10000; *m = ymd / 100 % 100; *d = ymd % 100;
}

static uint32_t JoinYmd(int y, unsigned m, unsigned d) {
    return (uint32_t)(y * 10000 + m * 100 + d);
}

uint32_t TodayLocal() {
    SYSTEMTIME t; GetLocalTime(&t);
    return JoinYmd(t.wYear, t.wMonth, t.wDay);
}

int NowHour() {
    SYSTEMTIME t; GetLocalTime(&t);
    return t.wHour;
}

uint32_t AddDays(uint32_t ymd, int delta) {
    int y; unsigned m, d;
    SplitYmd(ymd, &y, &m, &d);
    civil_from_days(days_from_civil(y, m, d) + delta, &y, &m, &d);
    return JoinYmd(y, m, d);
}

int DayDiff(uint32_t a, uint32_t b) {
    int y1, y2; unsigned m1, m2, d1, d2;
    SplitYmd(a, &y1, &m1, &d1);
    SplitYmd(b, &y2, &m2, &d2);
    return (int)(days_from_civil(y1, m1, d1) - days_from_civil(y2, m2, d2));
}

std::wstring YmdToStr(uint32_t ymd) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%04u-%02u-%02u", ymd / 10000, ymd / 100 % 100, ymd % 100);
    return buf;
}

std::string FormatEventTimeNow() {
    SYSTEMTIME t; GetLocalTime(&t);
    char buf[40];
    snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}
