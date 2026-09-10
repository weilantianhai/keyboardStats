#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 初始化：创建设置目录 %APPDATA%\KeyboardStats，
// 载入当前数据（显式数据文件，或数据文件夹下的全部 events-*.jsonl）构建按天缓存。
void StorageInit();

// 钩子回调调用：事件计入当日聚合、事件行入缓冲。必须快，不做磁盘 I/O。
void RecordKey(uint8_t vk);

// 由主窗口 WM_TIMER 周期调用：满 5 秒且缓冲非空则追加落盘。
void StorageFlushIfDue();

// 退出前强制落盘。
void StorageFlushNow();

// ────────────────────────── 数据位置 ──────────────────────────
// 设置目录（偏好文件所在，固定）与数据文件夹（可自选，默认等于设置目录）是两回事。

// 设置目录（ui-theme.txt / ui-font.txt / ui-data.txt 所在）
std::wstring StorageSettingsDir();

// 数据文件夹：ui-data.txt 的 dir= 指定；未指定或目录已不存在时回落到默认数据文件夹
std::wstring DataFolderPath();

// 默认数据文件夹：<exe 目录>\keyboardstats（不存在则自动创建）。
// 若该位置写不了（比如程序装在 Program Files），退回设置目录。
// KEYBOARDSTATS_DIR 环境变量优先，测试/便携运行仍然可控。
std::wstring DefaultDataFolder();

// 目录是否可写（预检：建一个临时文件再删掉）。目录不存在或只读都返回 false。
bool CanWriteToFolder(const std::wstring& dir);

// 当前数据文件名（相对数据文件夹）；空 = 自动（按月 events-YYYYMM.jsonl）
std::wstring DataFileName();

// 当前数据文件绝对路径（自动模式下为当月 events-YYYYMM.jsonl）
std::wstring DataFilePath();

// 切换数据文件夹的结果
struct FolderSwitchResult {
    int moved = 0;    // 成功搬过去的文件数
    int failed = 0;   // 搬运失败的文件数（被占用等）
};

// 切换数据文件夹：目录不存在则创建，写偏好并重新载入。
// 切换前先做写入预检，没权限（例如需要管理员）时直接失败并给出原因，不做任何改动。
// moveFiles=true 时把原数据文件夹里的数据文件一起搬过去。
// 若要求搬运但一个都没成功，则中止切换（原文件夹保持不变），避免"数据留在原地却切走了"。
bool StorageSetDataFolder(const std::wstring& dir, bool moveFiles, std::wstring* error,
                          FolderSwitchResult* result = nullptr);

// 切换数据文件：name 为空表示恢复自动（按月）。文件必须已在数据文件夹内。
bool StorageSetDataFile(const std::wstring& name, std::wstring* error);

// 新建数据文件：在数据文件夹里创建 data-YYYYMMDD-HHMMSS.jsonl（写入标记行）并切换为当前数据文件。
// createdName 回传新文件名。
bool StorageCreateDataFile(std::wstring* createdName, std::wstring* error);

// ────────────────────────── 数据文件识别 ──────────────────────────

// 数据文件第一行是标记行，用于把它和普通文本/表格文件区分开
enum class DataFileKind {
    Invalid,   // 不是数据文件
    Marked,    // 第一行带标记行
    Legacy,    // 无标记，但每一行都是可识别的事件（旧版本文件，兼容接受）
};

// 探测文件类型（只读，不会改动文件）
DataFileKind StorageProbeFile(const std::wstring& path);

// 数据文件标记行的文本（含换行）
const char* StorageMagicLine();

// 列出某文件夹里的数据文件：带标记的 *.jsonl、名字形如 events-*.jsonl 的，
// 以及当前显式指定的数据文件（若它就在该文件夹里）
std::vector<std::wstring> DataFilesInFolder(const std::wstring& dir);

// ────────────────────────── 查询 ──────────────────────────

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

struct StorageInfo {
    long events = 0;        // 事件总条数
    int  files = 0;         // 数据文件数
    long long bytes = 0;    // 数据文件占用字节数
    uint32_t firstYmd = 0;  // 最早有记录的日期（0 = 无记录）
    uint32_t lastYmd = 0;   // 最近有记录的日期
};
StorageInfo StorageDescribe();

// 导出：JSONL（原样事件，按时序）或 CSV（时间,键码,名称）
bool StorageExportJsonl(const std::wstring& path, long* outCount);
bool StorageExportCsv(const std::wstring& path, long* outCount);

// 转入数据文件：把 path 指向的 JSONL **移动**到数据文件夹（重名则自动改名），
// 并切换为当前数据文件后重新载入。原位置不再保留该文件。
// 返回载入后的事件条数；失败返回 -1 并在 error 写入原因。
long StorageAdoptJsonl(const std::wstring& path, std::wstring* error);

// 清除全部记录（清空当前数据 + 内存缓存；顺带清理旧版本遗留的 counts.json）
void StorageClearAll();
