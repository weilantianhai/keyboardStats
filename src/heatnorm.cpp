// 热力归一化实现：峰值拆分 + 按筛选模式取除数。
// 纯逻辑，无框架依赖（见 heatnorm.h）。
#include "heatnorm.h"

#include <algorithm>

namespace app {

long g_maxKey = 0;
long g_maxKeyboard = 0;
long g_maxMouseClick = 0;
long g_maxWheel = 0;
int  g_keyFilter = 2;   // 默认"全部"

void HeatMaximaFromCounts(const long counts[256]) {
    g_maxKey = 0;
    g_maxKeyboard = 0;
    g_maxMouseClick = 0;
    g_maxWheel = 0;
    // 峰值不只是一个数：全局峰值供"全部"模式跨组对比，另外三组各留一份，
    // 供键盘 / 鼠标 / 分开模式按组独立归一（滚轮不再碾压点击次数）。
    for (int vk = 0; vk < 256; ++vk) {
        const long c = counts[vk];
        g_maxKey = std::max(g_maxKey, c);
        switch (KeyGroupOf((uint8_t)vk)) {
            case KeyGroup::Wheel:       g_maxWheel      = std::max(g_maxWheel, c); break;
            case KeyGroup::MouseButton: g_maxMouseClick = std::max(g_maxMouseClick, c); break;
            case KeyGroup::Keyboard:    g_maxKeyboard   = std::max(g_maxKeyboard, c); break;
        }
    }
}

double HeatNorm(uint8_t vk, long count) {
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
        case 3:   // 分开：三组各自独立
            divisor = (group == KeyGroup::Wheel)       ? g_maxWheel
                    : (group == KeyGroup::MouseButton) ? g_maxMouseClick
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
