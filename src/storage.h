#pragma once
#include "layout.h"   // KeyCode / kKeySlots：键码空间（键鼠 + 手柄）定义
#include <windows.h>  // DWORD / INFINITE：数据变化通知的等待接口
#include <cstdint>
#include <string>
#include <vector>

// 初始化：创建设置目录 %APPDATA%\KeyboardStats，
// 载入当前数据（显式数据文件，或数据文件夹下的全部 events-*.jsonl）构建按天缓存。
void StorageInit();

// 钩子回调调用：事件计入当日聚合、事件行入缓冲。必须快，不做磁盘 I/O。
void RecordKey(KeyCode vk);

// 手柄模拟量行程增量提交（采集线程每个采样周期调用一次）。
// 传的是**路程长度**增量（不是位置）：
//   stickL / stickR = 该周期内左右摇杆走过的归一化路程（1.0 = 一个满量程半径）
//   trigL  / trigR  = 该周期内左右扳机行程增量（1.0 = 从松到底一次）
// 内部按（日, 时）聚合，不逐次落盘——模拟量每秒可产生几十次增量，逐次写会把文件撑爆。
void PadTravelAdd(float stickL, float stickR, float trigL, float trigR);

// 手柄行程的累计结果（"里程表"读数）。
// 单位是**等效次数**：满推往返次数 / 满按到底次数（显示层再按偏好折算成毫米）。
struct PadTravel {
    double stickL = 0.0;   // 左摇杆累计等效满推往返次数
    double stickR = 0.0;   // 右摇杆
    double trigL  = 0.0;   // 左扳机累计等效满按次数
    double trigR  = 0.0;   // 右扳机
};

// 查询某时间段内累计的手柄行程。mode/from/to 语义与 QueryRange 一致
// （0=今天 1=最近7天 2=最近30天 3=全部 4=自定义）。
PadTravel PadTravelQuery(int mode, uint32_t from, uint32_t to);

// 今日累计的手柄行程（看板/活跃分数用，等价于 PadTravelQuery(0,0,0)）
PadTravel PadTravelToday();

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
    long counts[kKeySlots] = {};            // 该时段内每键次数（键鼠 + 手柄统一索引）
    long total = 0;
    std::vector<Bucket> buckets;            // 直方图柱，已按时间排序
};

// mode: 0=今天 1=最近7天 2=最近30天 3=全部 4=自定义[from,to]
RangeStats QueryRange(int mode, uint32_t from, uint32_t to);

// 今日键鼠拆分（主页看板用）
struct TodayBreakdown {
    long keyboard = 0;     // 键盘按键（不含鼠标/滚轮）
    long mouseClicks = 0;  // 鼠标点击（左右/中/侧键，不含滚轮）
    long wheel = 0;        // 滚轮格数
    long pad = 0;          // 手柄按键（XInput 数字键）
};
TodayBreakdown StorageTodayBreakdown();

// 有记录的天数（按日聚合表非空天数）
long StorageActiveDayCount();

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

// 合并数据文件：把数据文件夹里**除当前文件外**的所有数据文件的事件行
// 追加到当前数据文件，成功后删除源文件。
// 场景：新建过多个 data-*.jsonl 或转入过文件，数据分散在多处，想并成一份。
// mergedFiles/mergedEvents 回传合并的文件数与事件条数（可为 nullptr）。
// 任一文件读取失败就跳过它（不删，数据不丢）；追加失败则中止并报错。
// 返回 true 表示至少合并了一个文件（没有可合并的文件时返回 false 且 error 说明）。
bool StorageMergeDataFiles(int* mergedFiles, long* mergedEvents, std::wstring* error);

// 清除全部记录（清空当前数据 + 内存缓存；顺带清理旧版本遗留的 counts.json）
void StorageClearAll();

// ────────────────────────── 双进程协作（GUI ⟷ 记录进程） ──────────────────────────
// 记录进程（--record）负责钩子和落盘；GUI 进程只读数据文件。两边通过命名事件通信。
// 事件在 StorageInit 时创建（CreateEvent 幂等，另一进程直接按名打开同名对象）。

// GUI 侧：数据文件被记录进程更新后增量重载内存缓存（只读新增部分，毫秒级）。
// 由 GUI 的定时器周期调用；兜底自记录模式下不要调用（会把自己的数据读重）。
void StorageReloadIfChanged();

// 全量重载：重读数据位置偏好 + 清内存缓存 + 重新读全部数据文件。
// 记录进程收到 Reload 事件时调用；也用于兜底场景。
void StorageReloadFull();

// GUI 在改动数据（清除/切换文件夹/切换文件/转入/新建）后调用：通知记录进程重载
void StorageNotifyPeers();

// GUI "退出程序"时调用：通知记录进程落盘并退出
void StorageSignalShutdown();

// 进程间对象名（互斥/事件，本会话内）
inline constexpr wchar_t kIpcRecorderMutex[] = L"Local\\KeyboardStats.Recorder";
inline constexpr wchar_t kIpcGuiMutex[]      = L"Local\\KeyboardStats.Gui";
inline constexpr wchar_t kIpcReload[]        = L"Local\\KeyboardStats.Reload";
inline constexpr wchar_t kIpcShutdown[]      = L"Local\\KeyboardStats.Shutdown";
// record → GUI 的**反向**数据变化通知（kIpcReload 是 GUI → record，方向相反）。
// 记录进程每次落盘后 SetEvent，GUI 收到才去读新增的那几行——不再靠定时器轮询文件。
inline constexpr wchar_t kIpcDataChanged[]   = L"Local\\KeyboardStats.DataChanged";

// 记录进程落盘后通知 GUI（有变更才通知；GUI 已关闭时事件对象随之销毁，SetEvent 无害失败）
void StorageNotifyDataChanged();
// GUI 侧等待数据变化：timeoutMs 传 INFINITE 阻塞等待，传 0 做非阻塞探测。
// 返回 true 表示期间收到过通知。
bool StorageWaitDataChanged(DWORD timeoutMs);
// 结束等待并释放句柄（GUI 关闭时调用；g_days 等内存缓存随进程退出自然释放）
void StorageStopDataChangedWait();
