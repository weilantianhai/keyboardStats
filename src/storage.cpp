#include "storage.h"
#include "timeutil.h"
#include "layout.h"
#include "pref.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
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
static std::vector<std::string> g_evBuf;    // 待落盘事件行
static double g_lastEvFlush = 0.0;          // 秒（GetTickCount64/1000）
static std::wstring g_dir;                  // 设置目录（偏好文件所在）
static std::wstring g_defaultDataDir;       // 默认数据文件夹（exe 同级 keyboardstats，启动时解析一次）
static std::wstring g_dataDir;              // 数据文件夹（ui-data.txt 的 dir= 指定）
static std::wstring g_dataFile;             // 当前数据文件名（空 = 自动按月）

// 双进程 IPC（事件句柄；定义在"双进程协作"一节，StorageInit 会先调用）
static HANDLE g_evReload = nullptr;
static HANDLE g_evShutdown = nullptr;
static void EnsureIpc();

// ────────────────────────── 小工具 ──────────────────────────

static double NowSec() { return GetTickCount64() / 1000.0; }

static bool DirExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// 文件名部分（去掉目录）
static std::wstring FileNameOf(const std::wstring& path) {
    const size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

// 目录部分（末尾无分隔符）
static std::wstring DirNameOf(const std::wstring& path) {
    const size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : path.substr(0, s);
}

static bool SamePath(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && !b.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static long long FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a = {};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1;
    return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

// ────────────────────────── 数据文件标记行 ──────────────────────────
// 第一行是标记行，用来把数据文件和普通文本/表格文件区分开。
// 用 JSON 而不是裸文本：jq 之类按行读 JSON 的工具不会报错，且能带版本号便于以后迁移。

static const char kMagicLine[] = "{\"format\":\"keyboardstats-data\",\"version\":1}\n";
static const char kMagicNeedle[] = "\"keyboardstats-data\"";

const char* StorageMagicLine() { return kMagicLine; }

static bool IsMagicLine(const char* line) {
    return strstr(line, kMagicNeedle) != nullptr;
}

// 文件不存在或为空时补写标记行（不重写已有内容的文件）
static void EnsureMagicLine(const std::wstring& path) {
    if (FileSize(path) > 0) return;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fputs(kMagicLine, f);
    fclose(f);
}

// 只读第一行判断是否带标记
static bool HasMagicLine(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char line[256] = {};
    const bool got = fgets(line, sizeof line, f) != nullptr;
    fclose(f);
    return got && IsMagicLine(line);
}

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

static void LoadEventFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        uint32_t ymd; int hour; uint8_t vk;
        if (ParseEventLine(line, &ymd, &hour, &vk)) ApplyEvent(ymd, hour, vk, 1);
    }
    fclose(f);
}

// 当前参与统计的数据文件集合：
// 显式指定了数据文件时只有它一个；否则是数据文件夹下的全部 events-*.jsonl（按月）。
static std::vector<std::wstring> DataFiles() {
    std::vector<std::wstring> out;
    if (!g_dataFile.empty()) {
        out.push_back(DataFolderPath() + L"\\" + g_dataFile);
        return out;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((DataFolderPath() + L"\\events-*.jsonl").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (wcslen(fd.cFileName) > 11) out.push_back(DataFolderPath() + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());   // 文件名含月份，升序即时间序
    return out;
}

// 把缓冲写入数据文件：显式指定了文件时整体追加到该文件，否则按月分组
static void FlushEvents() {
    if (g_evBuf.empty()) return;

    if (!g_dataFile.empty()) {
        EnsureMagicLine(DataFilePath());
        FILE* f = _wfopen(DataFilePath().c_str(), L"ab");
        if (f) {
            for (const auto& line : g_evBuf) fwrite(line.data(), 1, line.size(), f);
            fclose(f);
            g_evBuf.clear();
        }
        // fopen 失败（文件被占用等）时**保留缓冲**，下个周期重试，避免静默丢数据
        return;
    }

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
        const std::wstring path = DataFolderPath() + name;
        EnsureMagicLine(path);   // 新月份文件先落标记行
        FILE* f = _wfopen(path.c_str(), L"ab");
        if (!f) continue;
        fwrite(chunk.data(), 1, chunk.size(), f);
        fclose(f);
    }
    g_evBuf.clear();
}

// ────────────────────────── 数据位置 ──────────────────────────

std::wstring StorageSettingsDir() { return g_dir.empty() ? app::PrefDirPath() : g_dir; }

// exe 所在目录（末尾无分隔符）
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const size_t s = std::wstring(buf).find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : std::wstring(buf).substr(0, s);
}

// 目录可写测试：建一个临时文件再删掉
bool CanWriteToFolder(const std::wstring& dir) {
    if (!DirExists(dir)) return false;
    static int seq = 0;
    wchar_t name[64];
    swprintf(name, 64, L"\\.kbstats-write-test-%u-%d", GetCurrentProcessId(), ++seq);
    const std::wstring probe = dir + name;
    FILE* f = _wfopen(probe.c_str(), L"wb");
    if (!f) return false;
    fputs("x", f);
    fclose(f);
    DeleteFileW(probe.c_str());
    return true;
}

std::wstring DefaultDataFolder() {
    // KEYBOARDSTATS_DIR 优先：测试与便携运行靠它，语义是"整个程序的家目录"
    wchar_t custom[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"KEYBOARDSTATS_DIR", custom, MAX_PATH) > 0)
        return std::wstring(custom);

    const std::wstring dir = ExeDir() + L"\\keyboardstats";
    if (DirExists(dir)) return dir;
    if (CreateDirectoryW(dir.c_str(), nullptr)) return dir;
    // 建不了（例如程序装在 Program Files）→ 退回设置目录，保证程序仍可用
    return StorageSettingsDir();
}

std::wstring DataFolderPath() {
    if (!g_dataDir.empty()) return g_dataDir;
    return g_defaultDataDir.empty() ? StorageSettingsDir() : g_defaultDataDir;
}

std::wstring DataFileName() { return g_dataFile; }

std::wstring DataFilePath() {
    std::wstring name = g_dataFile;
    if (name.empty()) {
        wchar_t buf[32];
        swprintf(buf, 32, L"events-%06u.jsonl", (unsigned)(TodayLocal() / 100));
        name = buf;
    }
    return DataFolderPath() + L"\\" + name;
}

// ────────────────────────── 数据文件识别 ──────────────────────────

DataFileKind StorageProbeFile(const std::wstring& path) {
    const long long size = FileSize(path);
    if (size <= 0) return DataFileKind::Invalid;

    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return DataFileKind::Invalid;

    bool marked = false;
    long events = 0, others = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (IsMagicLine(line)) { marked = true; continue; }
        if (line[0] == '\n' || line[0] == '\r') continue;   // 空行忽略
        uint32_t ymd = 0;
        int hour = 0;
        uint8_t vk = 0;
        if (ParseEventLine(line, &ymd, &hour, &vk)) ++events;
        else if (++others > 3) break;                        // 连续杂行 → 不是数据文件
    }
    fclose(f);

    if (marked) return DataFileKind::Marked;
    // 旧版本文件没有标记行：内容全是事件行时仍认（否则升级后自己的旧导出会被拒）
    if (events > 0 && others == 0) return DataFileKind::Legacy;
    return DataFileKind::Invalid;
}

std::vector<std::wstring> DataFilesInFolder(const std::wstring& dir) {
    std::vector<std::wstring> out;
    if (!DirExists(dir)) return out;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.jsonl").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring name = fd.cFileName;
            const std::wstring path = dir + L"\\" + name;
            // 名字形如 events-*.jsonl 的按月文件，或第一行带标记的数据文件
            const bool byName = wcsncmp(name.c_str(), L"events-", 7) == 0 && name.size() > 11;
            if (byName || HasMagicLine(path)) out.push_back(path);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // 当前显式指定的数据文件（可能是导出的自定义名，既不像 events-* 也没标记）
    if (!g_dataFile.empty()) {
        const std::wstring cur = DataFolderPath() + L"\\" + g_dataFile;
        if (SamePath(DirNameOf(cur), dir)) {
            bool dup = false;
            for (const std::wstring& p : out) if (SamePath(p, cur)) { dup = true; break; }
            if (!dup && FileSize(cur) >= 0) out.push_back(cur);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ────────────────────────── 生命周期 ──────────────────────────

// 读 ui-data.txt：dir= 数据文件夹，file= 当前数据文件名（空 = 自动）
static void LoadDataPrefs() {
    g_dataDir = app::Widen(app::PrefGetValue(L"ui-data.txt", "dir", ""));
    if (!DirExists(g_dataDir)) g_dataDir.clear();   // 目录被删/被拔掉 → 回落到设置目录

    g_dataFile = app::Widen(app::PrefGetValue(L"ui-data.txt", "file", ""));
    if (!g_dataFile.empty()) {
        const DWORD a = GetFileAttributesW((DataFolderPath() + L"\\" + g_dataFile).c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
            g_dataFile.clear();   // 文件已不在数据文件夹 → 回到自动模式
        }
    }
}

// 默认数据文件夹从 %APPDATA% 改到 exe 同级后，老用户的数据还在旧位置，
// 直接切过去会看到统计"变空了"。这里做一次性拷贝把数据带过来：
// 只拷贝、不移动、不删除，旧数据原样保留作备份；新位置已有数据则不动。
static void AdoptLegacyDataIfNeeded() {
    if (!g_dataDir.empty()) return;                  // 用户显式指定过位置 → 不动
    const std::wstring target = DataFolderPath();
    if (SamePath(target, g_dir)) return;             // 默认位置就是设置目录 → 无需搬运
    if (!DataFilesInFolder(target).empty()) return;  // 新位置已有数据 → 不覆盖
    const std::vector<std::wstring> legacy = DataFilesInFolder(g_dir);
    if (legacy.empty()) return;
    for (const std::wstring& src : legacy) {
        CopyFileW(src.c_str(), (target + L"\\" + FileNameOf(src)).c_str(), TRUE);
    }
}

void StorageInit() {
    g_dir = app::PrefDirPath();
    CreateDirectoryW(g_dir.c_str(), nullptr);   // 已存在时返回 false，无妨
    g_defaultDataDir = DefaultDataFolder();
    LoadDataPrefs();
    AdoptLegacyDataIfNeeded();

    g_days.clear();
    for (const std::wstring& f : DataFiles()) LoadEventFile(f);

    EnsureIpc();
}

void RecordKey(uint8_t vk) {
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

// ────────────────────────── 双进程协作 ──────────────────────────

// CreateEvent 幂等：已存在时只是打开（GUI/记录进程谁先起都行）。
// 用**自动复位**事件：SetEvent 唤醒一个等待者后自动清零，即使消费端 ResetEvent
// 权限不足也不会陷入"永远 signaled → 反复触发"（实测踩过这个坑，见 recorder.cpp）。
static void EnsureIpc() {
    if (!g_evReload)   g_evReload   = CreateEventW(nullptr, FALSE, FALSE, kIpcReload);
    if (!g_evShutdown) g_evShutdown = CreateEventW(nullptr, FALSE, FALSE, kIpcShutdown);
}

void StorageNotifyPeers() {
    EnsureIpc();
    if (g_evReload) SetEvent(g_evReload);
}

void StorageSignalShutdown() {
    EnsureIpc();
    if (g_evShutdown) SetEvent(g_evShutdown);
}

void StorageReloadFull() {
    LoadDataPrefs();     // 数据文件夹/文件可能被 GUI 侧改过
    g_evBuf.clear();     // 丢弃未落盘缓冲，与 GUI 看到的文件内容保持一致
    g_days.clear();
    for (const std::wstring& f : DataFiles()) LoadEventFile(f);
}

// 增量重载：记录进程每 5 秒追加落盘，GUI 只需把"新增的尾巴"读进来。
// 文件集合或目录变化、文件变短（被清除/替换）时退回全量重载。
void StorageReloadIfChanged() {
    struct Pos { std::wstring path; long long size; };
    static std::vector<Pos> s_pos;
    static bool s_init = false;

    const std::vector<std::wstring> files = DataFiles();

    // 全量重载 + 重建位置快照
    auto reloadFull = [&] {
        StorageReloadFull();
        s_pos.clear();
        for (const std::wstring& p : DataFiles()) {
            WIN32_FILE_ATTRIBUTE_DATA a = {};
            if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a))
                s_pos.push_back({p, ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow});
        }
        s_init = true;
    };

    if (!s_init) { reloadFull(); return; }

    // 文件集合变化（新增/删除/切换目录）→ 全量
    bool full = files.size() != s_pos.size();
    if (!full) {
        for (size_t i = 0; i < files.size(); ++i)
            if (!SamePath(files[i], s_pos[i].path)) { full = true; break; }
    }
    if (full) { reloadFull(); return; }

    // 逐文件读新增尾巴（记录进程只追加不改写，所以从上次偏移读到 EOF 即可）
    for (auto& pos : s_pos) {
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        if (!GetFileAttributesExW(pos.path.c_str(), GetFileExInfoStandard, &a)) {
            reloadFull();   // 文件消失（被清除）→ 全量
            return;
        }
        const long long sz = ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        if (sz == pos.size) continue;
        if (sz < pos.size) { reloadFull(); return; }   // 被清空/替换
        FILE* f = _wfopen(pos.path.c_str(), L"rb");
        if (f) {
            _fseeki64(f, pos.size, SEEK_SET);
            char line[256];
            while (fgets(line, sizeof line, f)) {
                uint32_t ymd; int hour; uint8_t vk;
                if (ParseEventLine(line, &ymd, &hour, &vk)) ApplyEvent(ymd, hour, vk, 1);
            }
            fclose(f);
        }
        pos.size = sz;
    }
}

// ────────────────────────── 时间段查询 ──────────────────────────

// mode: 0=今天 1=最近7天 2=最近30天 3=全部 4=自定义[from,to]
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

// ────────────────────────── 记录管理 ──────────────────────────

static bool IsEventFile(const wchar_t* name) {
    return wcsncmp(name, L"events-", 7) == 0 && wcslen(name) > 11;
}

// 遍历当前数据文件集合；callback 收到每一行原始文本
static void ForEachEventLine(const std::function<void(const std::string&)>& fn) {
    for (const std::wstring& path : DataFiles()) {
        // 自动模式下只认 events-*.jsonl；显式指定的文件无论名字都算
        if (g_dataFile.empty() && !IsEventFile(FileNameOf(path).c_str())) continue;
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            const size_t len = strlen(line);
            if (len == 0 || line[0] != '{') continue;
            if (IsMagicLine(line)) continue;   // 标记行由导出方自己补，不当事件行
            fn(std::string(line, len));
        }
        fclose(f);
    }
}

StorageInfo StorageDescribe() {
    StorageInfo info;
    ForEachEventLine([&](const std::string& line) {
        uint32_t ymd = 0;
        int hour = 0;
        uint8_t vk = 0;
        if (!ParseEventLine(line.c_str(), &ymd, &hour, &vk)) return;
        ++info.events;
        if (info.firstYmd == 0 || ymd < info.firstYmd) info.firstYmd = ymd;
        if (ymd > info.lastYmd) info.lastYmd = ymd;
    });
    const std::vector<std::wstring> files = DataFiles();
    info.files = (int)files.size();
    for (const std::wstring& p : files) {
        WIN32_FILE_ATTRIBUTE_DATA attr = {};
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &attr))
            info.bytes += ((long long)attr.nFileSizeHigh << 32) | attr.nFileSizeLow;
    }
    return info;
}

bool StorageExportJsonl(const std::wstring& path, long* outCount) {
    StorageFlushNow();   // 先把内存缓冲落盘，保证导出完整
    FILE* out = _wfopen(path.c_str(), L"wb");
    if (!out) return false;
    fputs(kMagicLine, out);   // 导出文件本身也是合法数据文件，带标记行
    long n = 0;
    ForEachEventLine([&](const std::string& line) {
        fputs(line.c_str(), out);
        ++n;
    });
    fclose(out);
    if (outCount) *outCount = n;
    return true;
}

bool StorageExportCsv(const std::wstring& path, long* outCount) {
    StorageFlushNow();
    FILE* out = _wfopen(path.c_str(), L"wb");
    if (!out) return false;
    fputs("\xEF\xBB\xBF", out);             // UTF-8 BOM：Excel 直接双击也能正确识别中文
    fputs("time,keycode,keyname\n", out);
    long n = 0;
    ForEachEventLine([&](const std::string& line) {
        uint32_t ymd = 0;
        int hour = 0;
        uint8_t vk = 0;
        if (!ParseEventLine(line.c_str(), &ymd, &hour, &vk)) return;
        // 时间：取 t 字段原文
        const char* tp = strstr(line.c_str(), "\"t\":\"");
        char stamp[32] = {};
        if (tp) {
            snprintf(stamp, sizeof stamp, "%.23s", tp + 5);
        }
        const wchar_t* nm = StatName(vk);
        std::string name;
        if (nm) {
            const int need = WideCharToMultiByte(CP_UTF8, 0, nm, -1, nullptr, 0, nullptr, nullptr);
            if (need > 1) {
                name.resize((size_t)need - 1);
                WideCharToMultiByte(CP_UTF8, 0, nm, -1, name.data(), need, nullptr, nullptr);
            }
        } else {
            name = "VK" + std::to_string(vk);
        }
        fprintf(out, "%s,%u,%s\n", stamp, (unsigned)vk, name.c_str());
        ++n;
    });
    fclose(out);
    if (outCount) *outCount = n;
    return true;
}

// 目标文件已存在时改名：name.jsonl → name-1.jsonl → name-2.jsonl …
static std::wstring UniqueDestName(const std::wstring& folder, const std::wstring& fileName) {
    const std::wstring stem = [&] {
        const size_t dot = fileName.rfind(L'.');
        return dot == std::wstring::npos ? fileName : fileName.substr(0, dot);
    }();
    const std::wstring ext = [&] {
        const size_t dot = fileName.rfind(L'.');
        return dot == std::wstring::npos ? std::wstring() : fileName.substr(dot);
    }();
    for (int i = 0; i < 1000; ++i) {
        wchar_t suffix[16] = {};
        if (i > 0) swprintf(suffix, 16, L"-%d", i);
        const std::wstring candidate = stem + suffix + ext;
        if (GetFileAttributesW((folder + L"\\" + candidate).c_str()) == INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return fileName;
}

long StorageAdoptJsonl(const std::wstring& path, std::wstring* error) {
    // 1) 先校验是不是合法的数据文件（标记行，或旧版本那种纯事件行文件）
    const DataFileKind kind = StorageProbeFile(path);
    if (kind == DataFileKind::Invalid) {
        if (error) *error = L"该文件不是合法的数据文件（缺少标记行，且不含可识别的事件行）";
        return -1;
    }

    // 2) 已在数据文件夹内 → 直接切换；否则移动进来
    const std::wstring folder = DataFolderPath();
    std::wstring destName = FileNameOf(path);
    if (SamePath(DirNameOf(path), folder)) {
        if (!SamePath(path, folder + L"\\" + destName)) destName = FileNameOf(path);
    } else {
        destName = UniqueDestName(folder, destName);
        const std::wstring dest = folder + L"\\" + destName;
        StorageFlushNow();   // 先落盘，避免移动过程中丢事件
        if (!MoveFileExW(path.c_str(), dest.c_str(),
                         MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
            if (error) *error = L"无法把文件移动到数据文件夹（目标可能已存在或无权限）";
            return -1;
        }
    }

    // 3) 切换为当前数据文件并重新载入
    app::PrefSetValue(L"ui-data.txt", "file", app::Narrow(destName).c_str());
    StorageInit();
    StorageNotifyPeers();

    long total = 0;
    for (auto& [day, d] : g_days) total += d.total;
    return total;
}

bool StorageSetDataFolder(const std::wstring& dir, bool moveFiles, std::wstring* error,
                          FolderSwitchResult* result) {
    if (result) *result = FolderSwitchResult{};

    // ── 预检：能不能建、能不能写 ──
    // 这一步是重点：以前不预检，遇到需要管理员权限的文件夹会先切过去、再一个个搬文件，
    // 最后只显示"移动了 0 个文件"，用户不知道发生了什么。
    if (!DirExists(dir) && !CreateDirectoryW(dir.c_str(), nullptr)) {
        if (error) *error = L"无法创建该文件夹（路径无效，或需要管理员权限）";
        return false;
    }
    if (!CanWriteToFolder(dir)) {
        if (error) *error = L"该文件夹没有写入权限（可能需要管理员权限），已取消切换";
        return false;
    }

    StorageFlushNow();
    const std::wstring oldFolder = DataFolderPath();

    if (!SamePath(oldFolder, dir)) {
        if (moveFiles) {
            // 把原数据文件夹里的数据文件一起搬过去
            int moved = 0, failed = 0;
            std::wstring explicitNew = g_dataFile;
            for (const std::wstring& src : DataFilesInFolder(oldFolder)) {
                const std::wstring destName = UniqueDestName(dir, FileNameOf(src));
                if (!MoveFileExW(src.c_str(), (dir + L"\\" + destName).c_str(),
                                 MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
                    ++failed;
                    continue;
                }
                ++moved;
                if (!g_dataFile.empty() && SamePath(src, oldFolder + L"\\" + g_dataFile))
                    explicitNew = destName;   // 当前数据文件重名时可能被改名
            }
            if (result) { result->moved = moved; result->failed = failed; }
            // 要求搬运却一个都没成功 → 中止切换，否则数据留在原文件夹、程序却已经切走了
            if (moved == 0 && failed > 0) {
                if (error) *error = L"没有文件能搬过去（可能被占用或没有权限），已取消切换";
                return false;
            }
            // 搬运失败的（比如被占用）就置空，回到自动模式，避免指向不存在的文件
            if (!explicitNew.empty() &&
                GetFileAttributesW((dir + L"\\" + explicitNew).c_str()) == INVALID_FILE_ATTRIBUTES)
                explicitNew.clear();
            g_dataFile = explicitNew;
        } else {
            // "仅切换"：旧文件夹的数据留在原地，新文件夹从空开始
            g_dataFile.clear();
        }
    }

    app::PrefSetValue(L"ui-data.txt", "dir", app::Narrow(dir).c_str());
    app::PrefSetValue(L"ui-data.txt", "file", app::Narrow(g_dataFile).c_str());
    StorageInit();
    StorageNotifyPeers();
    return true;
}

bool StorageCreateDataFile(std::wstring* createdName, std::wstring* error) {
    const std::wstring folder = DataFolderPath();
    if (!DirExists(folder) && !CreateDirectoryW(folder.c_str(), nullptr)) {
        if (error) *error = L"数据文件夹不可用";
        return false;
    }
    StorageFlushNow();

    // 文件名带时间戳，天然不重名
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"data-%04u%02u%02u-%02u%02u%02u.jsonl",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring fileName = name;
    if (GetFileAttributesW((folder + L"\\" + fileName).c_str()) != INVALID_FILE_ATTRIBUTES)
        fileName = UniqueDestName(folder, fileName);

    FILE* f = _wfopen((folder + L"\\" + fileName).c_str(), L"wb");
    if (!f) {
        if (error) *error = L"无法创建文件（无权限或磁盘已满）";
        return false;
    }
    fputs(kMagicLine, f);   // 新文件先写标记行
    fclose(f);

    app::PrefSetValue(L"ui-data.txt", "file", app::Narrow(fileName).c_str());
    StorageInit();
    StorageNotifyPeers();

    if (createdName) *createdName = fileName;
    (void)error;
    return true;
}

bool StorageSetDataFile(const std::wstring& name, std::wstring* error) {
    if (!name.empty() &&
        GetFileAttributesW((DataFolderPath() + L"\\" + name).c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (error) *error = L"该文件不在数据文件夹内";
        return false;
    }
    StorageFlushNow();
    app::PrefSetValue(L"ui-data.txt", "file", app::Narrow(name).c_str());
    StorageInit();
    StorageNotifyPeers();
    return true;
}

void StorageClearAll() {
    g_evBuf.clear();
    g_days.clear();

    for (const std::wstring& path : DataFiles()) {
        if (!g_dataFile.empty()) {
            // 显式数据文件：清空内容但保留文件本身（它可能就是用户自己的那个文件），
            // 并补回标记行，使清空后它仍是合法数据文件
            FILE* f = _wfopen(path.c_str(), L"wb");
            if (f) {
                fputs(kMagicLine, f);
                fclose(f);
            }
        } else {
            DeleteFileW(path.c_str());
        }
    }
    // 旧版本遗留的 counts.json（现行版本不再写入，仅在此顺带清理）
    DeleteFileW((StorageSettingsDir() + L"\\counts.json").c_str());

    // 通知记录进程：数据已清空，重载（顺带清它内存里的计数与未落盘缓冲）
    StorageNotifyPeers();
}
