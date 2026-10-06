#pragma once
#include <cstdint>

// ── 键码空间 ──
// 统计/着色/实时状态统一用 KeyCode 索引：
//   0x00-0xFF  键盘虚拟键码（VK_*）与鼠标/滚轮伪键码（见下方 kMouse*/kWheel*）
//   0x100-0x1FF 手柄按键（本文件 kPad* 段；XInput 设备）
// 旧的 uint8_t 键码在加入手柄后不够用，故统一升为 uint16_t。
using KeyCode = uint16_t;

// 键码表的槽位数（counts[] 等数组长度）：覆盖上面两个区段
inline constexpr int kKeySlots = 512;

// 单个按键的绘制定义。坐标单位 = 标准键宽(1u)。
// x 范围：主键盘区 0-15，导航区 15.5-18.5，小键盘区 19.5-23.5。
struct KeyDef {
    KeyCode  vk;
    float    x, y, w, h;   // 位置与尺寸（单位 u，y 以行为单位）
    const wchar_t* cap;    // 键帽文字
};

// 104 键 ANSI 布局
inline constexpr KeyDef kKeys[] = {
    // ── 功能行 ──
    {0x1B, 0,0,1,1, L"Esc"},
    {0x70, 2,0,1,1, L"F1"},  {0x71, 3,0,1,1, L"F2"},  {0x72, 4,0,1,1, L"F3"},  {0x73, 5,0,1,1, L"F4"},
    {0x74, 6.5f,0,1,1, L"F5"}, {0x75, 7.5f,0,1,1, L"F6"}, {0x76, 8.5f,0,1,1, L"F7"}, {0x77, 9.5f,0,1,1, L"F8"},
    {0x78, 11,0,1,1, L"F9"}, {0x79, 12,0,1,1, L"F10"}, {0x7A, 13,0,1,1, L"F11"}, {0x7B, 14,0,1,1, L"F12"},
    // ── 数字行 ──
    {0xC0, 0,1,1,1, L"`"},   {0x31, 1,1,1,1, L"1"},   {0x32, 2,1,1,1, L"2"},   {0x33, 3,1,1,1, L"3"},
    {0x34, 4,1,1,1, L"4"},   {0x35, 5,1,1,1, L"5"},   {0x36, 6,1,1,1, L"6"},   {0x37, 7,1,1,1, L"7"},
    {0x38, 8,1,1,1, L"8"},   {0x39, 9,1,1,1, L"9"},   {0x30, 10,1,1,1, L"0"},
    {0xBD, 11,1,1,1, L"-"},  {0xBB, 12,1,1,1, L"="},  {0x08, 13,1,2,1, L"Backspace"},
    // ── QWERTY 行 ──
    {0x09, 0,2,1.5f,1, L"Tab"},
    {0x51, 1.5f,2,1,1, L"Q"}, {0x57, 2.5f,2,1,1, L"W"}, {0x45, 3.5f,2,1,1, L"E"}, {0x52, 4.5f,2,1,1, L"R"},
    {0x54, 5.5f,2,1,1, L"T"}, {0x59, 6.5f,2,1,1, L"Y"}, {0x55, 7.5f,2,1,1, L"U"}, {0x49, 8.5f,2,1,1, L"I"},
    {0x4F, 9.5f,2,1,1, L"O"}, {0x50, 10.5f,2,1,1, L"P"},
    {0xDB, 11.5f,2,1,1, L"["}, {0xDD, 12.5f,2,1,1, L"]"}, {0xDC, 13.5f,2,1.5f,1, L"\\"},
    // ── Home 行 ──
    {0x14, 0,3,1.75f,1, L"Caps"},
    {0x41, 1.75f,3,1,1, L"A"}, {0x53, 2.75f,3,1,1, L"S"}, {0x44, 3.75f,3,1,1, L"D"}, {0x46, 4.75f,3,1,1, L"F"},
    {0x47, 5.75f,3,1,1, L"G"}, {0x48, 6.75f,3,1,1, L"H"}, {0x4A, 7.75f,3,1,1, L"J"}, {0x4B, 8.75f,3,1,1, L"K"},
    {0x4C, 9.75f,3,1,1, L"L"},
    {0xBA, 10.75f,3,1,1, L";"}, {0xDE, 11.75f,3,1,1, L"'"}, {0x0D, 12.75f,3,2.25f,1, L"Enter"},
    // ── Shift 行 ──
    {0xA0, 0,4,2.25f,1, L"Shift"},
    {0x5A, 2.25f,4,1,1, L"Z"}, {0x58, 3.25f,4,1,1, L"X"}, {0x43, 4.25f,4,1,1, L"C"}, {0x56, 5.25f,4,1,1, L"V"},
    {0x42, 6.25f,4,1,1, L"B"}, {0x4E, 7.25f,4,1,1, L"N"}, {0x4D, 8.25f,4,1,1, L"M"},
    {0xBC, 9.25f,4,1,1, L","}, {0xBE, 10.25f,4,1,1, L"."}, {0xBF, 11.25f,4,1,1, L"/"},
    {0xA1, 12.25f,4,2.75f,1, L"Shift"},
    // ── 底行 ──
    {0xA2, 0,5,1.25f,1, L"Ctrl"},   {0x5B, 1.25f,5,1.25f,1, L"Win"},  {0xA4, 2.5f,5,1.25f,1, L"Alt"},
    {0x20, 3.75f,5,6.25f,1, L"Space"},
    {0xA5, 10,5,1.25f,1, L"Alt"},   {0x5C, 11.25f,5,1.25f,1, L"Fn"},  {0x5D, 12.5f,5,1.25f,1, L"Menu"},
    {0xA3, 13.75f,5,1.25f,1, L"Ctrl"},
    // ── 导航区 ──
    {0x2C, 15.5f,0,1,1, L"PrtSc"}, {0x91, 16.5f,0,1,1, L"ScrLk"}, {0x13, 17.5f,0,1,1, L"Pause"},
    {0x2D, 15.5f,1,1,1, L"Ins"},   {0x24, 16.5f,1,1,1, L"Home"},  {0x21, 17.5f,1,1,1, L"PgUp"},
    {0x2E, 15.5f,2,1,1, L"Del"},   {0x23, 16.5f,2,1,1, L"End"},   {0x22, 17.5f,2,1,1, L"PgDn"},
    {0x26, 16.5f,4,1,1, L"↑"},
    {0x25, 15.5f,5,1,1, L"←"},     {0x28, 16.5f,5,1,1, L"↓"},     {0x27, 17.5f,5,1,1, L"→"},
    // ── 小键盘（整体下移一行，使数字区最底部与主键区最底部对齐）──
    {0x90, 19.5f,1,1,1, L"Num"},  {0x6F, 20.5f,1,1,1, L"/"},   {0x6A, 21.5f,1,1,1, L"*"},  {0x6D, 22.5f,1,1,1, L"-"},
    {0x67, 19.5f,2,1,1, L"7"},    {0x68, 20.5f,2,1,1, L"8"},   {0x69, 21.5f,2,1,1, L"9"},  {0x6B, 22.5f,2,1,2, L"+"},
    {0x64, 19.5f,3,1,1, L"4"},    {0x65, 20.5f,3,1,1, L"5"},   {0x66, 21.5f,3,1,1, L"6"},
    {0x61, 19.5f,4,1,1, L"1"},    {0x62, 20.5f,4,1,1, L"2"},   {0x63, 21.5f,4,1,1, L"3"},  {0x0D, 22.5f,4,1,2, L"Enter"},
    {0x60, 19.5f,5,2,1, L"0"},    {0x6E, 21.5f,5,1,1, L"."},
};

// 鼠标按键与滚轮使用独立的伪键码（记录在同一张 counts[] 表里）
inline constexpr KeyCode kMouseLeft = 0x01;
inline constexpr KeyCode kMouseRight = 0x02;
inline constexpr KeyCode kMouseMiddle = 0x04;
inline constexpr KeyCode kMouseX1 = 0x05;
inline constexpr KeyCode kMouseX2 = 0x06;
inline constexpr KeyCode kWheelUp = 0xE0;
inline constexpr KeyCode kWheelDown = 0xE1;
inline constexpr KeyCode kWheelLeft = 0xE2;
inline constexpr KeyCode kWheelRight = 0xE3;

// ── 手柄按键（XInput 设备）──
// 独立段 0x100 起：与键盘 VK 码、鼠标/滚轮伪键码完全不重叠（键盘钩子收到的
// vkCode 上限是 0xFF，所以这一段永远不会被键鼠事件误命中）。
// 只收"数字量"按键；摇杆位移与扳机行程是模拟量，本阶段不计数。
inline constexpr KeyCode kPadBase = 0x100;
inline constexpr KeyCode kPadA      = kPadBase + 0;   // A
inline constexpr KeyCode kPadB      = kPadBase + 1;   // B
inline constexpr KeyCode kPadX      = kPadBase + 2;   // X
inline constexpr KeyCode kPadY      = kPadBase + 3;   // Y
inline constexpr KeyCode kPadLB     = kPadBase + 4;   // 左肩键
inline constexpr KeyCode kPadRB     = kPadBase + 5;   // 右肩键
inline constexpr KeyCode kPadView   = kPadBase + 6;   // View（Back）
inline constexpr KeyCode kPadMenu   = kPadBase + 7;   // Menu（Start）
inline constexpr KeyCode kPadLS     = kPadBase + 8;   // 左摇杆按下
inline constexpr KeyCode kPadRS     = kPadBase + 9;   // 右摇杆按下
inline constexpr KeyCode kPadUp     = kPadBase + 10;  // 十字键上
inline constexpr KeyCode kPadDown   = kPadBase + 11;  // 十字键下
inline constexpr KeyCode kPadLeft   = kPadBase + 12;  // 十字键左
inline constexpr KeyCode kPadRight  = kPadBase + 13;  // 十字键右
// 摇杆/扳机是**模拟量**，没有"按下次数"，只有走了多远。
// 这四个是**虚拟键码**：唯一用途是让"按键计数"列表和直方图能显示它们，
// 计数 = 行程折算出的等效次数（见 app::FetchStats）。
// 它们刻意**不在** kPadBits（上升沿采集位表）与 kPadButtons（面板真实控件）里——
// 前者只认按键边沿，后者是面板上画得出来的按钮，模拟量两个都不适用。
inline constexpr KeyCode kPadStickL = kPadBase + 14;  // 左摇杆行程
inline constexpr KeyCode kPadStickR = kPadBase + 15;  // 右摇杆行程
inline constexpr KeyCode kPadTrigL  = kPadBase + 16;  // LT 扳机行程
inline constexpr KeyCode kPadTrigR  = kPadBase + 17;  // RT 扳机行程
inline constexpr KeyCode kPadEnd    = kPadBase + 18;  // 段末（不含）

// ── 手柄面板的绘制定义 ──
// 设计坐标系：宽 0..kPadLayoutW、高 0..kPadLayoutH（原点左上，等比铺满面板中间区域）。
// 形状分三类：Rect（肩键、十字键四向热力块）/ Round（ABXY、View/Menu、摇杆圈）/ Poly（十字键外框）。
struct PadPt { float x, y; };
enum class PadShape { Rect, Round, Poly };

struct PadDef {
    KeyCode        vk;      // 统计键码
    PadShape       shape;
    float          x, y, w, h;   // Rect/Round：位置与尺寸；Poly：仅用于悬浮锚点
    const wchar_t* cap;          // 面板标签（圆内/块内文字）
    const PadPt*   pts;          // 保留字段：Poly 顶点（当前无使用者，供后续扩展）
    int            ptCount;
};

// 机身轮廓：来自开源项目 e7d/gamepad-viewer（MIT）的 Xbox One 皮肤
// （templates/xbox-one/base-white.svg 的 path0，MIT License © Michaël "e7d" Ferrand，
//  许可证副本见 .vendor/gv/LICENSE）。原始 SVG 是贝塞尔插画，这里已展平并用
//  Douglas-Peucker 抽稀到 49 个顶点；坐标从 750x630 画布线性映射到 12x8 设计框。
// 两个握把、底部中央内凹都是原始轮廓的一部分；内凹中央略抬高过（6.32），
// 否则右摇杆的大圈会被缺口切掉——test_gamepad ⑦ 的"点在机身内"断言盯着这件事。
inline constexpr PadPt kPadBody[] = {
    {10.21, 1.42}, {10.07, 1.21}, {9.86, 1.04},
    {9.74, 0.73}, {9.65, 0.63}, {8.90, 0.27},
    {8.21, 0.10}, {7.91, 0.12}, {7.52, 0.34},
    {7.34, 0.39}, {4.66, 0.39}, {4.48, 0.34},
    {4.09, 0.12}, {3.79, 0.10}, {3.10, 0.27},
    {2.40, 0.60}, {2.26, 0.73}, {2.14, 1.04},
    {1.93, 1.21}, {1.79, 1.42}, {1.28, 2.86},
    {0.82, 4.43}, {0.63, 5.37}, {0.55, 6.11},
    {0.63, 6.87}, {0.79, 7.28}, {1.12, 7.66},
    {1.35, 7.80}, {1.66, 7.90}, {2.08, 7.61},
    {3.36, 6.27}, {3.75, 5.99}, {3.93, 6.18},
    {5.91, 6.32}, {7.89, 6.15}, {8.25, 5.99},
    {8.64, 6.27}, {9.92, 7.61}, {10.21, 7.85},
    {10.40, 7.89}, {10.76, 7.74}, {11.11, 7.43},
    {11.30, 7.09}, {11.42, 6.62}, {11.44, 5.88},
    {11.32, 5.08}, {11.09, 4.07}, {10.72, 2.86},
    {10.21, 1.42},
};
inline constexpr int kPadBodyPointCount = 49;

// 机身外接框（绘制缩放与几何自检用）
inline constexpr float kPadBodyRectX = 0.55f;   // 机身左边界
inline constexpr float kPadBodyRectY = 0.10f;   // 机身上边界
inline constexpr float kPadBodyRectW = 10.89f;  // 机身宽（右边界 11.44）
inline constexpr float kPadBodyRectH = 7.80f;   // 机身高度（下边界 7.90）

// 肩键 LB / RB：顶部左右的横向胶囊，贴在机身顶边内（尺寸/位置取自同一皮肤的
// bumper 定位表：170x61 @ (107,129) 与 (473,129)，换算到设计坐标）。
inline constexpr float kPadBumperW = 2.47f;
inline constexpr float kPadBumperH = 0.77f;
inline constexpr float kPadBumperY = 0.14f;         // 上边界
inline constexpr float kPadBumperLX = 2.11f;        // LB 左边界
inline constexpr float kPadBumperRX = 7.42f;        // RB 左边界
// 保留顶点数组形式以兼容自检代码：给出肩键矩形的两对角点
inline constexpr PadPt kPadLBPoly[] = {
    {kPadBumperLX, kPadBumperY}, {kPadBumperLX + kPadBumperW, kPadBumperY},
    {kPadBumperLX + kPadBumperW, kPadBumperY + kPadBumperH}, {kPadBumperLX, kPadBumperY + kPadBumperH},
};
inline constexpr PadPt kPadRBPoly[] = {
    {kPadBumperRX, kPadBumperY}, {kPadBumperRX + kPadBumperW, kPadBumperY},
    {kPadBumperRX + kPadBumperW, kPadBumperY + kPadBumperH}, {kPadBumperRX, kPadBumperY + kPadBumperH},
};

// 十字键外框（十字形；四个方向的热力块画在四条臂上）
// 位置取自同一皮肤 dpad 定位表：容器 110x111 @ (223,345)，中心 (4.59, 4.32)，
// 臂长（中心到端）0.79、臂半宽 0.25（34px 宽 / 2）。
inline constexpr float kPadDpadCx = 4.59f;
inline constexpr float kPadDpadCy = 4.32f;
inline constexpr float kPadDpadArm = 0.79f;   // 臂向外伸出的长度
inline constexpr float kPadDpadHalf = 0.25f;  // 臂的半宽
inline constexpr PadPt kPadDpadPoly[] = {
    {kPadDpadCx - kPadDpadHalf, kPadDpadCy - kPadDpadHalf},
    {kPadDpadCx + kPadDpadHalf, kPadDpadCy - kPadDpadHalf},
    {kPadDpadCx + kPadDpadHalf, kPadDpadCy - kPadDpadArm},
    {kPadDpadCx + kPadDpadArm, kPadDpadCy - kPadDpadArm},
    {kPadDpadCx + kPadDpadArm, kPadDpadCy + kPadDpadArm},
    {kPadDpadCx + kPadDpadHalf, kPadDpadCy + kPadDpadArm},
    {kPadDpadCx + kPadDpadHalf, kPadDpadCy + kPadDpadHalf},
    {kPadDpadCx - kPadDpadHalf, kPadDpadCy + kPadDpadHalf},
    {kPadDpadCx - kPadDpadHalf, kPadDpadCy + kPadDpadArm},
    {kPadDpadCx - kPadDpadArm, kPadDpadCy + kPadDpadArm},
    {kPadDpadCx - kPadDpadArm, kPadDpadCy - kPadDpadArm},
    {kPadDpadCx - kPadDpadHalf, kPadDpadCy - kPadDpadArm},
};

// 摇杆：range 圈圆心与半径（内圈位置按实时模拟量偏移，见 DrawPadPanel）
//
// 位置取自同一皮肤 sticks 定位表：左帽中心 (185.5, 280.5)、右帽中心 (473.5, 393.5)，
// 帽直径 83px = 1.21 设计单位。大圈半径 0.72（比帽略大，做"可动范围"底座），
// 内圈 0.30。真机 Xbox 布局：右摇杆比 ABXY 靠内，整体呈"八"字形——这里照原样保留。
// 间距校验（test_gamepad ⑦ 会逐条复核）：
//   · 右摇杆下沿 y = 4.21 + 0.72 = 4.93，机身底部内凹最低点 6.32 → 余量 1.39；
//   · A 键 x 8.40..9.17 与右摇杆 x 6.71..8.15 完全错开，互不相交；
//   · 两圈圆心距 √(4.18² + 1.75²) ≈ 4.53 > 半径和 1.44。
struct PadStickDef {
    KeyCode vk;          // 摇杆按下（L3/R3）的键码
    float   cx, cy;      // 圆心（设计坐标）
    float   range;       // 大圈半径 = 内圈可移动范围
    float   dot;         // 内圈半径
    int     axis;        // 模拟量在 SharedPadAnalog 中的下标（0=左 1=右）
};
inline constexpr PadStickDef kPadSticks[] = {
    {kPadLS, 3.25f, 2.46f, 0.72f, 0.30f, 0},
    {kPadRS, 7.43f, 4.21f, 0.72f, 0.30f, 1},
};
inline constexpr int kPadStickCount = sizeof(kPadSticks) / sizeof(kPadSticks[0]);

// 其余按键（ABXY / View·Menu / 十字键四向）
// 坐标取自同一皮肤的 buttons / start-select 定位表（53px 键径 → 0.77，
// 四向偏移 0.73；View/Menu 33px → 0.48）。
//   Y=上 / X=左 / B=右 / A=下。
inline constexpr float kPadDiamondCx = 8.78f;
inline constexpr float kPadDiamondCy = 2.44f;
inline constexpr float kPadFaceOff = 0.73f;    // 菱形四向偏移
inline constexpr float kPadFaceDia = 0.77f;    // 面键直径
inline constexpr PadDef kPadButtons[] = {
    // ABXY 菱形
    {kPadY, PadShape::Round, kPadDiamondCx - kPadFaceDia * 0.5f, kPadDiamondCy - kPadFaceOff - kPadFaceDia * 0.5f, kPadFaceDia, kPadFaceDia, L"Y", nullptr, 0},
    {kPadX, PadShape::Round, kPadDiamondCx - kPadFaceOff - kPadFaceDia * 0.5f, kPadDiamondCy - kPadFaceDia * 0.5f, kPadFaceDia, kPadFaceDia, L"X", nullptr, 0},
    {kPadB, PadShape::Round, kPadDiamondCx + kPadFaceOff - kPadFaceDia * 0.5f, kPadDiamondCy - kPadFaceDia * 0.5f, kPadFaceDia, kPadFaceDia, L"B", nullptr, 0},
    {kPadA, PadShape::Round, kPadDiamondCx - kPadFaceDia * 0.5f, kPadDiamondCy + kPadFaceOff - kPadFaceDia * 0.5f, kPadFaceDia, kPadFaceDia, L"A", nullptr, 0},
    // View / Menu（中央偏上，关于机身中线 x=6.0 严格对称：中心 5.24 / 6.81）
    {kPadView, PadShape::Round, 5.00f, 2.22f, 0.48f, 0.48f, L"-", nullptr, 0},
    {kPadMenu, PadShape::Round, 6.57f, 2.22f, 0.48f, 0.48f, L"+", nullptr, 0},
    // 肩键（顶部左/右两角，半嵌入机身）
    {kPadLB, PadShape::Rect, kPadBumperLX, kPadBumperY, kPadBumperW, kPadBumperH, L"LB", nullptr, 0},
    {kPadRB, PadShape::Rect, kPadBumperRX, kPadBumperY, kPadBumperW, kPadBumperH, L"RB", nullptr, 0},
    // 十字键四向（十字形四条臂上的热力块，与外框同心；中心那小块由外框填充，
    // 四臂各自独立不互相重叠——重叠会导致同一区域被多次绘制、热力色互相覆盖）
    {kPadUp,    PadShape::Rect, kPadDpadCx - kPadDpadHalf, kPadDpadCy - kPadDpadArm,
     2.0f * kPadDpadHalf, kPadDpadArm - kPadDpadHalf, L"", nullptr, 0},
    {kPadDown,  PadShape::Rect, kPadDpadCx - kPadDpadHalf, kPadDpadCy + kPadDpadHalf,
     2.0f * kPadDpadHalf, kPadDpadArm - kPadDpadHalf, L"", nullptr, 0},
    {kPadLeft,  PadShape::Rect, kPadDpadCx - kPadDpadArm, kPadDpadCy - kPadDpadHalf,
     kPadDpadArm - kPadDpadHalf, 2.0f * kPadDpadHalf, L"", nullptr, 0},
    {kPadRight, PadShape::Rect, kPadDpadCx + kPadDpadHalf, kPadDpadCy - kPadDpadHalf,
     kPadDpadArm - kPadDpadHalf, 2.0f * kPadDpadHalf, L"", nullptr, 0},
};
inline constexpr int kPadButtonCount = sizeof(kPadButtons) / sizeof(kPadButtons[0]);
inline constexpr float kPadLayoutW = 12.0f;   // 设计坐标系的宽（用于换算缩放）
inline constexpr float kPadLayoutH = 8.0f;    // 设计坐标系的高

// 扳机柱状图（面板左右两侧的竖直条）：行程 0..255 映射为条高
inline constexpr float kPadBarLabelGap = 0.34f;   // 标签与条之间留的间隙（设计坐标单位）

// 手柄按钮编码位（XInput 的 XINPUT_GAMEPAD_* 值）：采集层按键位表判断按下
struct PadBitDef {
    KeyCode vk;
    uint16_t bit;
};
inline constexpr PadBitDef kPadBits[] = {
    {kPadUp,    0x0001}, {kPadDown,  0x0002}, {kPadLeft,  0x0004}, {kPadRight, 0x0008},
    {kPadMenu,  0x0010}, {kPadView,  0x0020},
    {kPadLS,    0x0040}, {kPadRS,    0x0080},
    {kPadLB,    0x0100}, {kPadRB,    0x0200},
    {kPadA,     0x1000}, {kPadB,     0x2000}, {kPadX,     0x4000}, {kPadY,     0x8000},
};
inline constexpr int kPadBitCount = sizeof(kPadBits) / sizeof(kPadBits[0]);

// 鼠标点击键（左/右/中/侧键）——不含滚轮
inline bool IsMouseButton(KeyCode vk) {
    return vk == kMouseLeft || vk == kMouseRight || vk == kMouseMiddle ||
           vk == kMouseX1 || vk == kMouseX2;
}

// 滚轮键（上/下/左/右）——单独一组，避免滚轮格数碾压点击次数
inline bool IsWheelKey(KeyCode vk) {
    return vk == kWheelUp || vk == kWheelDown || vk == kWheelLeft || vk == kWheelRight;
}

// 手柄按键（0x100 段）
inline bool IsPadKey(KeyCode vk) { return vk >= kPadBase && vk < kPadEnd; }

// 是否为鼠标/滚轮伪键（用于分区统计与筛选）
inline bool IsMouseKey(KeyCode vk) { return IsMouseButton(vk) || IsWheelKey(vk); }

// 热力归一化分组：四组各自独立求峰值（见 app::HeatNorm）
enum class KeyGroup { Keyboard, MouseButton, Wheel, Gamepad };

inline KeyGroup KeyGroupOf(KeyCode vk) {
    if (IsPadKey(vk)) return KeyGroup::Gamepad;
    if (IsWheelKey(vk)) return KeyGroup::Wheel;
    if (IsMouseButton(vk)) return KeyGroup::MouseButton;
    return KeyGroup::Keyboard;
}

inline constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

// 手柄按键的统计名称（排行榜/悬浮提示用）
inline const wchar_t* PadName(KeyCode vk) {
    switch (vk) {
        case kPadA:     return L"手柄A";
        case kPadB:     return L"手柄B";
        case kPadX:     return L"手柄X";
        case kPadY:     return L"手柄Y";
        case kPadLB:    return L"手柄LB";
        case kPadRB:    return L"手柄RB";
        case kPadView:  return L"手柄View";
        case kPadMenu:  return L"手柄Menu";
        case kPadLS:    return L"左摇杆按下";
        case kPadRS:    return L"右摇杆按下";
        case kPadUp:    return L"十字键上";
        case kPadDown:  return L"十字键下";
        case kPadLeft:  return L"十字键左";
        case kPadRight: return L"十字键右";
        // 虚拟键码（模拟量），列表里按"等效次数"显示
        case kPadStickL: return L"左摇杆";
        case kPadStickR: return L"右摇杆";
        case kPadTrigL:  return L"左扳机";
        case kPadTrigR:  return L"右扳机";
    }
    return nullptr;
}

// 这四个虚拟键码是不是模拟量（摇杆/扳机）
inline bool IsPadAnalogKey(KeyCode vk) {
    return vk == kPadStickL || vk == kPadStickR ||
           vk == kPadTrigL  || vk == kPadTrigR;
}

// 统计排行用的名称（中文优先，字母数字用原字符）
inline const wchar_t* StatName(KeyCode vk) {
    if (const wchar_t* p = PadName(vk)) return p;
    switch (vk) {
        case 0x20: return L"空格";   case 0x0D: return L"回车";
        case 0x08: return L"退格";   case 0x09: return L"Tab";
        case 0x14: return L"Caps";   case 0x1B: return L"Esc";
        case 0x5D: return L"菜单";   case 0x2E: return L"删除";
        case 0x2D: return L"插入";
        case 0x24: return L"Home";   case 0x23: return L"End";
        case 0x21: return L"PgUp";   case 0x22: return L"PgDn";
        case 0x25: return L"←";      case 0x26: return L"↑";
        case 0x27: return L"→";      case 0x28: return L"↓";
        case 0x2C: return L"PrtSc";  case 0x91: return L"ScrLk";  case 0x13: return L"Pause";
        case 0x90: return L"NumLk";  case 0x6F: return L"小键盘/"; case 0x6A: return L"小键盘*";
        case 0x6D: return L"小键盘-"; case 0x6B: return L"小键盘+"; case 0x6E: return L"小键盘.";
        case 0xC0: return L"`";      case 0xBD: return L"-";      case 0xBB: return L"=";
        case 0xDB: return L"[";      case 0xDD: return L"]";      case 0xDC: return L"\\";
        case 0xBA: return L";";      case 0xDE: return L"'";
        case 0xBC: return L",";      case 0xBE: return L".";      case 0xBF: return L"/";
    }
    if (vk >= 0x70 && vk <= 0x7B) {           // F1-F12
        static const wchar_t* f[12] = { L"F1",L"F2",L"F3",L"F4",L"F5",L"F6",L"F7",L"F8",L"F9",L"F10",L"F11",L"F12" };
        return f[vk - 0x70];
    }
    if (vk >= 'A' && vk <= 'Z') {             // A-Z
        static const wchar_t* l[26] = { L"A",L"B",L"C",L"D",L"E",L"F",L"G",L"H",L"I",L"J",L"K",L"L",L"M",
                                        L"N",L"O",L"P",L"Q",L"R",L"S",L"T",L"U",L"V",L"W",L"X",L"Y",L"Z" };
        return l[vk - 'A'];
    }
    if (vk >= '0' && vk <= '9') {             // 0-9
        static const wchar_t* d[10] = { L"0",L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9" };
        return d[vk - '0'];
    }
    if (vk >= 0x60 && vk <= 0x69) {           // 小键盘数字：加 ` 后缀与主键区数字区分
        static const wchar_t* n[10] = { L"0`",L"1`",L"2`",L"3`",L"4`",
                                        L"5`",L"6`",L"7`",L"8`",L"9`" };
        return n[vk - 0x60];
    }
    switch (vk) {
        case 0xA0: case 0xA1: return L"Shift";
        case 0xA2: case 0xA3: return L"Ctrl";
        case 0xA4: case 0xA5: return L"Alt";
        case 0x5B: return L"Win";
        case 0x5C: return L"Fn";   // 右侧 Win 位置多为 Fn（本机键盘即如此）
        case kMouseLeft:   return L"鼠标左键";
        case kMouseRight:  return L"鼠标右键";
        case kMouseMiddle: return L"鼠标中键";
        case kMouseX1:     return L"鼠标侧键1";
        case kMouseX2:     return L"鼠标侧键2";
        case kWheelUp:     return L"滚轮上";
        case kWheelDown:   return L"滚轮下";
        case kWheelLeft:   return L"滚轮左";
        case kWheelRight:  return L"滚轮右";
    }
    return nullptr;
}
