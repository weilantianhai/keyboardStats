#include "storage.h"
#include "timeutil.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// ────────────────────────── 数据模型 ──────────────────────────

// 单日聚合（含当天实时更新）。月度柱由每日数据二次聚合。
struct DayAgg {
    long total = 0;
    long byHour[24] = {};
    std::map<uint8_t, long> byKey;
};

static std::map<uint32_t, DayAgg> g_days;   // yyyymmdd → 聚合（唯一数据真相源）
static long g_counts[256] = {};             // 全时段每键计数（由 g_days 推导的缓存）
static std::vector<std::string> g_evBuf;    // 待落盘事件行
static double g_lastEvFlush = 0.0;          // 秒（GetTickCount64/1000）
static std::wstring g_dir;                  // 数据目录

// ────────────────────────── 小工具 ──────────────────────────

static std::wstring DataDir() {
    // 测试/便携口子：设置 KEYBOARDSTATS_DIR 时用指定目录，否则用 %APPDATA%
    wchar_t custom[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"KEYBOARDSTATS_DIR", custom, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return custom;
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    return std::wstring(appdata) + L"\\KeyboardStats";
}

static double NowSec() { return GetTickCount64() / 1000.0; }

// ────────────────────────── events jsonl ──────────────────────────
// 每行：{"k":65,"t":"2026-01-15T09:30:12.345"}

static void ApplyEvent(uint32_t ymd, int hour, uint8_t vk, long n) {
    DayAgg& d = g_days[ymd];
    d.total += n;
    if (hour >= 0 && hour < 24) d.byHour[hour] += n;
    d.byKey[vk] += n;
}

static bool ParseEventLine(const char* line, uint32_t* ymd, int* hour, uint8_t* vk) {
    const char* kp = strstr(line, "\"k\":");
    if (!kp) return false;
    long code = atol(kp + 4);
    if (code <= 0 || code > 255) return false;
    const char* tp = strstr(line, "\"t\":\"");
    if (!tp) return false;
    int y, mo, d, h, mi, s, ms;
    if (sscanf(tp + 5, "%d-%d-%dT%d:%d:%d.%d", &y, &mo, &d, &h, &mi, &s, &ms) < 3) return false;
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23) return false;
    *ymd = (uint32_t)(y * 10000 + mo * 100 + d);
    *hour = h;
    *vk = (uint8_t)code;
    return true;
}

static void LoadEventMonth(uint32_t ym) {
    wchar_t name[64];
    swprintf(name, 64, L"\\events-%06u.jsonl", (unsigned)ym);
    FILE* f = _wfopen((g_dir + name).c_str(), L"rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        uint32_t ymd; int hour; uint8_t vk;
        if (ParseEventLine(line, &ymd, &hour, &vk)) ApplyEvent(ymd, hour, vk, 1);
    }
    fclose(f);
}

// 把缓冲按月份分组追加到对应 events 文件
static void FlushEvents() {
    if (g_evBuf.empty()) return;
    std::map<uint32_t, std::string> byMonth;   // ym → 拼接块
    for (const auto& line : g_evBuf) {
        // 行格式固定：...-YYYYMM-DDT...，从 t 字段取 ym
        size_t tp = line.find("\"t\":\"");
        if (tp == std::string::npos) continue;
        int y = atoi(line.c_str() + tp + 5);
        int mo = atoi(line.c_str() + tp + 10);
        byMonth[(uint32_t)(y * 100 + mo)] += line;
    }
    for (auto& [ym, chunk] : byMonth) {
        wchar_t name[64];
        swprintf(name, 64, L"\\events-%06u.jsonl", (unsigned)ym);
        FILE* f = _wfopen((g_dir + name).c_str(), L"ab");
        if (!f) continue;
        fwrite(chunk.data(), 1, chunk.size(), f);
        fclose(f);
    }
    g_evBuf.clear();
}

// ────────────────────────── 生命周期 ──────────────────────────

void StorageInit() {
    g_dir = DataDir();
    CreateDirectoryW(g_dir.c_str(), nullptr);   // 已存在时返回 false，无妨
    // 扫描全部月份事件文件，构建按日聚合（唯一数据真相源）
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"\\events-*.jsonl").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            uint32_t ym = (uint32_t)_wtoi(fd.cFileName + wcslen(L"events-"));
            if (ym >= 200001) LoadEventMonth(ym);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // 由按日聚合推导全时段计数缓存
    memset(g_counts, 0, sizeof g_counts);
    for (auto& [day, d] : g_days)
        for (auto& [vk, n] : d.byKey) g_counts[vk] += n;
}

void RecordKey(uint8_t vk) {
    g_counts[vk]++;
    uint32_t today = TodayLocal();
    ApplyEvent(today, NowHour(), vk, 1);
    if (g_evBuf.capacity() == 0) g_evBuf.reserve(256);
    if (g_evBuf.size() < 4096)
        g_evBuf.push_back("{\"k\":" + std::to_string(vk) + ",\"t\":\"" + FormatEventTimeNow() + "\"}\n");
}

void StorageFlushIfDue() {
    double now = NowSec();
    if (!g_evBuf.empty() && now - g_lastEvFlush >= 5.0) {
        FlushEvents();
        g_lastEvFlush = now;
    }
}

void StorageFlushNow() { FlushEvents(); }

long AllTimeCount(uint8_t vk) { return g_counts[vk]; }

// ────────────────────────── 时间段查询 ──────────────────────────

RangeStats QueryRange(int mode, uint32_t from, uint32_t to) {
    RangeStats rs;
    uint32_t today = TodayLocal();
    uint32_t dFrom, dTo;

    switch (mode) {
        case 0: dFrom = dTo = today; break;
        case 1: dFrom = AddDays(today, -6);            dTo = today; break;
        case 2: dFrom = AddDays(today, -29);           dTo = today; break;
        case 3: dFrom = 0;                             dTo = 99999999; break;
        default:
            dFrom = from < to ? from : to;
            dTo   = from < to ? to : from;
            if (dFrom == 0) { dTo = 99999999; }            // 未选日期 → 全部
            break;
    }

    bool isAll = dTo >= 99999999;
    bool singleDay = !isAll && dFrom == dTo;
    bool byDay = !isAll && !singleDay && DayDiff(dTo, dFrom) <= 61;   // ≤62 天按天出柱

    // 1) 建直方图桶骨架
    std::map<uint32_t, size_t> bucketIdx;   // 键：日(yyyymmdd) 或 月(yyyymm) → 桶下标
    if (singleDay) {
        for (int h = 0; h < 24; ++h) rs.buckets.push_back({ std::to_wstring(h) + L"时", 0 });
    } else if (byDay) {
        for (uint32_t d = dFrom; d <= dTo; d = AddDays(d, 1)) {
            bucketIdx[d] = rs.buckets.size();
            rs.buckets.push_back({ YmdToStr(d).substr(5), 0 });   // L"01-15"
        }
    } else {                                            // 全部 / 长自定义 → 按月
        uint32_t prevM = 0;
        for (auto& [day, d] : g_days) {
            if (day < dFrom || day > dTo) continue;
            uint32_t m = day / 100;
            if (m != prevM) {
                bucketIdx[m] = rs.buckets.size();
                wchar_t lbl[16];
                swprintf(lbl, 16, L"%04u-%02u", m / 100, m % 100);
                rs.buckets.push_back({ lbl, 0 });
                prevM = m;
            }
        }
    }

    // 2) 汇总每日数据
    for (auto& [day, d] : g_days) {
        if (day < dFrom || day > dTo) continue;
        rs.total += d.total;
        for (auto& [vk, n] : d.byKey) rs.counts[vk] += n;

        if (singleDay) {
            for (int h = 0; h < 24; ++h) rs.buckets[h].count += d.byHour[h];
        } else {
            auto it = bucketIdx.find(byDay ? day : day / 100);
            if (it != bucketIdx.end()) rs.buckets[it->second].count += d.total;
        }
    }
    return rs;
}
