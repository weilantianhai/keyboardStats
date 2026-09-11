#pragma once
#include <cstdint>

// 单个按键的绘制定义。坐标单位 = 标准键宽(1u)。
// x 范围：主键盘区 0-15，导航区 15.5-18.5，小键盘区 19.5-23.5。
struct KeyDef {
    uint8_t  vk;
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
    {0xA5, 10,5,1.25f,1, L"Alt"},   {0x5C, 11.25f,5,1.25f,1, L"Win"}, {0x5D, 12.5f,5,1.25f,1, L"Menu"},
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

// 鼠标按键与滚轮使用独立的伪键码（记录在同一张 counts[256] 表里）
inline constexpr uint8_t kMouseLeft = 0x01;
inline constexpr uint8_t kMouseRight = 0x02;
inline constexpr uint8_t kMouseMiddle = 0x04;
inline constexpr uint8_t kMouseX1 = 0x05;
inline constexpr uint8_t kMouseX2 = 0x06;
inline constexpr uint8_t kWheelUp = 0xE0;
inline constexpr uint8_t kWheelDown = 0xE1;
inline constexpr uint8_t kWheelLeft = 0xE2;
inline constexpr uint8_t kWheelRight = 0xE3;

// 鼠标点击键（左/右/中/侧键）——不含滚轮
inline bool IsMouseButton(uint8_t vk) {
    return vk == kMouseLeft || vk == kMouseRight || vk == kMouseMiddle ||
           vk == kMouseX1 || vk == kMouseX2;
}

// 滚轮键（上/下/左/右）——单独一组，避免滚轮格数碾压点击次数
inline bool IsWheelKey(uint8_t vk) {
    return vk == kWheelUp || vk == kWheelDown || vk == kWheelLeft || vk == kWheelRight;
}

// 是否为鼠标/滚轮伪键（用于分区统计与筛选）
inline bool IsMouseKey(uint8_t vk) { return IsMouseButton(vk) || IsWheelKey(vk); }

// 热力归一化分组：三组各自独立求峰值（见 app::HeatNorm）
enum class KeyGroup { Keyboard, MouseButton, Wheel };

inline KeyGroup KeyGroupOf(uint8_t vk) {
    if (IsWheelKey(vk)) return KeyGroup::Wheel;
    if (IsMouseButton(vk)) return KeyGroup::MouseButton;
    return KeyGroup::Keyboard;
}

inline constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

// 统计排行用的名称（中文优先，字母数字用原字符）
inline const wchar_t* StatName(uint8_t vk) {
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
        case 0x5B: case 0x5C: return L"Win";
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
