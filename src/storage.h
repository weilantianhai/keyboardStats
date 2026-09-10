#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 初始化：创建数据目录 %APPDATA%\KeyboardStats，
// 加载 counts.json 到内存，解析全部 events-*.jsonl 构建按天缓存。
void StorageInit();

// 钩子回调调用：内存计数 +1、事件行入缓冲。必须快，不做磁盘 I/O。
void RecordKey(uint8_t vk);

// 由主窗口 WM_TIMER 周期调用：满 5 秒且缓冲非空则追加落盘；
// counts 有变更且距上次重写超 30 秒则重写 counts.json。
void StorageFlushIfDue();

// 退出前强制落盘（events + counts）。
void StorageFlushNow();

// 全时段累计计数（counts.json + 本次运行增量）。
long AllTimeCount(uint8_t vk);

// 一个直方图柱。
struct Bucket {
    std::wstring label;   // "0时" / "01-15" / "2026-01"
    long count;
};

// 一个时间段上的统计结果（热力图 + 直方图共用）。
struct RangeStats {
    long counts[256] = {};                  // 该时段内每键次数
    long total = 0;
    std::vector<Bucket> buckets;            // 直方图柱，已按时间排序
};

// mode: 0=今天 1=最近7天 2=最近30天 3=全部 4=自定义[from,to]
RangeStats QueryRange(int mode, uint32_t from, uint32_t to);

// ────────────────────────── 记录管理 ──────────────────────────

// 数据目录（绝对路径，末尾无分隔符）
std::wstring StorageDirPath();

struct StorageInfo {
    long events = 0;        // 事件总条数
    int  files = 0;         // 月度事件文件数
    long long bytes = 0;    // 数据目录占用字节数
    uint32_t firstYmd = 0;  // 最早有记录的日期（0 = 无记录）
    uint32_t lastYmd = 0;   // 最近有记录的日期
};
StorageInfo StorageDescribe();

// 导出：JSONL（原样事件，按时序）或 CSV（时间,键码,名称）
bool StorageExportJsonl(const std::wstring& path, long* outCount);
bool StorageExportCsv(const std::wstring& path, long* outCount);

// 导入 JSONL 事件文件（格式见 README）：按时间戳归入对应月份，返回导入条数；
// 失败时 error 写入原因。
long StorageImportJsonl(const std::wstring& path, std::wstring* error);

// 清除全部记录（事件文件 + counts.json + 内存缓存）
void StorageClearAll();
