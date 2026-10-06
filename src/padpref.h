#pragma once
// 手柄模拟量记录的运行偏好：采样率档位 + 显示单位。
//
// 为什么要单独一个文件：pref.h 只是通用的 key=value 读写，没有"哪些取值算合法"的语义。
// 采样率/单位要被三处共同使用——采集层（gamepad.cpp 决定轮询间隔）、设置页（渲染选项）、
// 显示层（把行程折算成用户要的单位），必须有**唯一**的取值定义与解析规则，
// 否则三处各写一套字符串判断，迟早出现"设置页存了 high、采集层只认 hi"这类静默不一致。
//
// 偏好文件：ui-pad.txt（与 ui-theme.txt / ui-font.txt 同目录）
//   rate=low|mid|high      摇杆/扳机的采样率档位
//   unit=count|mm          行程的显示单位
#include "pref.h"

namespace app {

// ── 采样率：XInput 本身约 125Hz 刷新，采样快于它没有收益，只会白烧 CPU ──
// 采样率只影响**模拟量行程累计**的精度与实时渲染刷新；按键是边沿触发，
// 三档都一样（两次采样之间按下再抬起会丢，这是轮询的固有缺陷，与档位无关）。
enum class PadRate { Low, Mid, High };

inline const wchar_t* kPadPrefFile = L"ui-pad.txt";

// 轮询间隔（毫秒）。有手柄时用这个间隔，无手柄仍走 1s 探测。
inline DWORD PadRatePollMs(PadRate r) {
    switch (r) {
        case PadRate::Low:  return 33;   // 约 30Hz：省 CPU，快速搓杆会明显低估行程
        case PadRate::Mid:  return 16;   // 约 62Hz：日常记录，快速搓杆轻微低估
        case PadRate::High: return 8;    // 约 125Hz：与 XInput 刷新率对齐，最准
    }
    return 16;
}

// 稳定字符串 id（写进偏好用 id，不用下标——下标会随枚举顺序变化而错位）
inline const char* PadRateId(PadRate r) {
    switch (r) {
        case PadRate::Low:  return "low";
        case PadRate::Mid:  return "mid";
        case PadRate::High: return "high";
    }
    return "mid";
}

inline const wchar_t* PadRateName(PadRate r) {
    switch (r) {
        case PadRate::Low:  return L"低";
        case PadRate::Mid:  return L"中";
        case PadRate::High: return L"高";
    }
    return L"中";
}

// 档位说明（设置页副标题用）
inline const wchar_t* PadRateDesc(PadRate r) {
    switch (r) {
        case PadRate::Low:  return L"约 30Hz · 省资源";
        case PadRate::Mid:  return L"约 62Hz · 均衡";
        case PadRate::High: return L"约 125Hz · 最精确";
    }
    return L"";
}

inline PadRate PadRateFromId(const std::string& id) {
    if (id == "low")  return PadRate::Low;
    if (id == "high") return PadRate::High;
    return PadRate::Mid;   // 默认中档；未知值也回落到中档
}

inline PadRate PadRateGet() {
    return PadRateFromId(PrefGetValue(kPadPrefFile, "rate", "mid"));
}

inline void PadRateSet(PadRate r) {
    PrefSetValue(kPadPrefFile, "rate", PadRateId(r));
}

inline constexpr int kPadRateCount = 3;
inline PadRate PadRateAt(int i) {
    switch (i) {
        case 0: return PadRate::Low;
        case 1: return PadRate::Mid;
        default: return PadRate::High;
    }
}

// ── 显示单位：行程换算成什么单位给人看 ──
//   count = 等效次数（摇杆"满推往返多少次"、扳机"满按到底多少次"）——最好理解
//   mm    = 毫米（指尖实际移动距离 / 按压总深度）——有物理实感，但数字很大
enum class PadUnit { Count, Mm };

inline const char* PadUnitId(PadUnit u) {
    switch (u) {
        case PadUnit::Count: return "count";
        case PadUnit::Mm:    return "mm";
    }
    return "count";
}

inline const wchar_t* PadUnitName(PadUnit u) {
    switch (u) {
        case PadUnit::Count: return L"等效次数";
        case PadUnit::Mm:    return L"毫米";
    }
    return L"";
}

inline const wchar_t* PadUnitDesc(PadUnit u) {
    switch (u) {
        case PadUnit::Count: return L"满推往返 / 满按次数";
        case PadUnit::Mm:    return L"指尖移动毫米数";
    }
    return L"";
}

inline PadUnit PadUnitFromId(const std::string& id) {
    if (id == "mm") return PadUnit::Mm;
    return PadUnit::Count;   // 默认等效次数（用户要求"便于理解"）
}

inline PadUnit PadUnitGet() {
    return PadUnitFromId(PrefGetValue(kPadPrefFile, "unit", "count"));
}

inline void PadUnitSet(PadUnit u) {
    PrefSetValue(kPadPrefFile, "unit", PadUnitId(u));
}

inline constexpr int kPadUnitCount = 2;
inline PadUnit PadUnitAt(int i) {
    return i == 0 ? PadUnit::Count : PadUnit::Mm;
}

// ── 单位换算系数（唯一来源，供显示层与活跃分数共用）──
// 一次按键的物理行程约 4mm（薄膜/机械轴都在 3.5~4mm），作为"1 分"的基准。
inline constexpr double kPadKeyTravelMm = 4.0;

// 摇杆：满推往返一次 = 2 倍半径 ≈ 60mm
inline constexpr double kPadStickRoundTripMm = 60.0;
// 扳机：满按到底 ≈ 10mm
inline constexpr double kPadTriggerFullMm = 10.0;

// 把"等效满推往返次数"折算成用户要的单位
inline double PadStickToDisplay(double roundTrips, PadUnit u) {
    return u == PadUnit::Mm ? roundTrips * kPadStickRoundTripMm : roundTrips;
}

// 把"等效满按次数"折算成用户要的单位
inline double PadTriggerToDisplay(double fullPresses, PadUnit u) {
    return u == PadUnit::Mm ? fullPresses * kPadTriggerFullMm : fullPresses;
}

inline const wchar_t* PadUnitSuffix(PadUnit u) {
    return u == PadUnit::Mm ? L"mm" : L"次";
}

// 排行榜/直方图里一个条目的计数后缀。
// 摇杆/扳机是**行程**不是次数，必须跟"行程显示单位"开关走（次 / mm）；
// 键鼠一律"次"。早先所有条目都硬写"次"，于是单位开关只写偏好、界面毫无变化。
inline const wchar_t* PadUnitSuffixForCount(bool analog) {
    return analog ? PadUnitSuffix(PadUnitGet()) : L"次";
}

// ── 活跃分数权重（唯一来源，界面说明与计算共用）──
// 基准：一次按键按下 = 1 分。
// 摇杆与扳机的权重都刻意压低：摇杆阻尼极低，游戏里长时间按住晃几乎不费力；
// 扳机虽比摇杆费力些，但赛车类游戏会长时间半扣着，同样不该与按键等权。
// 若按物理行程给权重（摇杆曾用 15）会刷出几万分，把按键分彻底淹没。
// 滚轮同理（一格几乎不费劲），沿用既有 0.1。
inline constexpr double kScorePerKey       = 1.0;    // 键盘按键 / 鼠标点击
inline constexpr double kScorePerWheel     = 0.1;    // 滚轮一格（沿用既有设定）
inline constexpr double kScorePerStickTrip = 0.3;    // 摇杆满推往返一次
inline constexpr double kScorePerTrigger   = 0.5;    // 扳机满按到底一次

// 界面上一句话说明基准（看板/设置页共用）。
// 写成"多少 X 记 1 分"是为了让用户能自己验算；分数取整后小数会丢，
// 故这里只在能凑整时才用"N 次 = 1 分"的说法，凑不整的直接写单次分值。
inline const wchar_t* kScoreBasisText =
    L"1 分 = 1 次按键；扳机满按 2 次、滚轮 10 格各记 1 分；摇杆满推往返一次记 0.3 分";

// ── 活跃分数计算（唯一实现）──
// 之所以放到这里而不是散在看板里：分数的"口径"必须和上面那几个权重常量挨着，
// 否则改了权重却漏改公式，界面上的说明与实际算出来的分就对不上了。
//
// 档位说明：行程多算一点或一点不算都没问题——用户要的是"各类操作相对多少"的直觉。
// 摇杆按**满推往返次数**算（不是归一化路程）：走满一个半径算半次往返，因此
// 权重 0.5 恰好等于"按摇杆权重算一次满推往返"。这让 kScorePerStickTrip 的
// 数字与界面文案（"满推往返 2 次记 1 分"）严格一致，用户自己能验算。
inline long PadScore(double keyboard, double mouseClicks, double wheel,
                     double stickRoundTrips, double triggerFullPresses) {
    return (long)(keyboard * kScorePerKey + mouseClicks * kScorePerKey +
                  wheel * kScorePerWheel + stickRoundTrips * kScorePerStickTrip +
                  triggerFullPresses * kScorePerTrigger);
}

// 摇杆的"归一化路程"折算成"等效满推往返次数"：一个满推往返 = 走 2 个半径
inline double PadStickTripsFromTravel(double travel) { return travel * 0.5; }

} // namespace app
