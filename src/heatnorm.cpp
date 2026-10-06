// 热力归一化实现：峰值拆分 + 按筛选模式取除数。
// 纯逻辑，无框架依赖（见 heatnorm.h）。
#include "heatnorm.h"

#include <algorithm>

namespace app {

long g_maxKey = 0;
long g_maxKeyboard = 0;
long g_maxMouseClick = 0;
long g_maxWheel = 0;
long g_maxGamepad = 0;
int  g_keyFilter = 3;   // 默认"全部"（筛选档位：0键盘 1鼠标 2手柄 3全部 4分开）

void HeatMaximaFromCounts(const long counts[kKeySlots]) {
    g_maxKey = 0;
    g_maxKeyboard = 0;
    g_maxMouseClick = 0;
    g_maxWheel = 0;
    g_maxGamepad = 0;
    // 峰值不只是一个数：全局峰值供"全部"模式跨组对比，各组另留一份，
    // 供键盘 / 鼠标 / 手柄 / 分开模式按组独立归一（滚轮不再碾压点击，键盘不再碾压手柄）。
    for (int vk = 0; vk < kKeySlots; ++vk) {
        const long c = counts[vk];
        if (c > g_maxKey) g_maxKey = c;
        switch (KeyGroupOf((KeyCode)vk)) {
            case KeyGroup::Wheel:       g_maxWheel      = std::max(g_maxWheel, c); break;
            case KeyGroup::MouseButton: g_maxMouseClick = std::max(g_maxMouseClick, c); break;
            case KeyGroup::Gamepad:     g_maxGamepad    = std::max(g_maxGamepad, c); break;
            case KeyGroup::Keyboard:    g_maxKeyboard   = std::max(g_maxKeyboard, c); break;
        }
    }
}

double HeatNorm(KeyCode vk, long count) {
    if (count <= 0) return 0.0;
    const KeyGroup group = KeyGroupOf(vk);
    long divisor = 0;
    switch (g_keyFilter) {
        case 0:   // 键盘：只看键盘
            divisor = (group == KeyGroup::Keyboard) ? g_maxKeyboard : 0;
            break;
        case 1:   // 鼠标：点击与滚轮各按本组峰值，互不碾压
            divisor = (group == KeyGroup::MouseButton) ? g_maxMouseClick
                    : (group == KeyGroup::Wheel)       ? g_maxWheel
                                                       : 0;
            break;
        case 2:   // 手柄：只看手柄
            divisor = (group == KeyGroup::Gamepad) ? g_maxGamepad : 0;
            break;
        case 4:   // 分开：四组各自独立（三个面板各显各的层次）
            divisor = (group == KeyGroup::Wheel)       ? g_maxWheel
                    : (group == KeyGroup::MouseButton) ? g_maxMouseClick
                    : (group == KeyGroup::Gamepad)     ? g_maxGamepad
                                                       : g_maxKeyboard;
            break;
        default:  // 全部（以及任何未知值）：跨组共用全局峰值
            divisor = g_maxKey;
            break;
    }
    if (divisor <= 0) return 0.0;
    return std::min(1.0, (double)count / (double)divisor);
}

} // namespace app
