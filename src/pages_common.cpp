// pages_*.cpp 共享的内部状态与小构件（声明见 pages_common.h）。
// 代码原样从 pages.cpp 搬出，行为不变。
#include "pages_common.h"
#include "theme.h"
#include "state.h"
#include "layout.h"
#include "ui_util.h"
#include "win/adminmode.h"

#include <algorithm>

namespace app {

// Top 10 侧栏展开状态（仅本文件；窄窗口自动隐藏侧栏）
bool s_top10Open = true;

// ────────────────── UI 小构件 ──────────────────

void MiniButton(core::dsl::Ui& ui, const std::string& id, float x, float y,
                float w, float h, const std::string& label, bool accent,
                std::function<void()> onClick) {
    core::Color base  = accent ? g_theme.selected : g_theme.panel;
    core::Color hover = accent ? Hex(0x3B82F6) : g_theme.panelHi;
    core::Color press = accent ? Hex(0x1D4ED8) : g_theme.panelActive;
    ui.rect(id + ".bg")
        .x(x).y(y).size(w, h)
        .states(base, hover, press)
        .radius(Px(8.0f))
        .border(1.0f, accent ? core::Color{0, 0, 0, 0} : g_theme.border)
        .onClick(std::move(onClick))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();
    ui.text(id + ".t")
        .x(x).y(y).size(w, h)
        .text(label)
        .fontSize(Px(17.0f))
        .lineHeight(Px(20.0f))
        .color(accent ? Hex(0xFFFFFF) : g_theme.text)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

// ────────────────── 版面尺寸（外盒/列表边界可无极调节，持久化） ──────────────────

// 管理员开关的闪烁引导（主界面提示框点击后跳转设置页时置位）
bool g_adminHighlight = false;

unsigned long long g_adminHighlightAt = 0;

void DebugHighlightAdmin() {
    g_page = 2;
    g_adminHighlight = true;
    g_adminHighlightAt = GetTickCount64();
}

bool AdminHighlightActive() {
    return g_adminHighlight && !RunningElevated()
        && (GetTickCount64() - g_adminHighlightAt) < kAdminHighlightMs;
}

// 滚动位置（跨帧保持）
eui::Signal<float> s_settingsScroll{0.0f};

eui::Signal<float> s_themeScroll{0.0f};

// 简易滚动容器：内容高度由调用方给出，内容回调每帧只跑一次。
// 不用框架 ScrollView 的自动测量——它会先在一个独立的测量 Ui 里把内容回调再跑一遍，
// 那一遍创建的交互元素会留下按"内容坐标"算的命中框；操作几次之后这些残留命中框会和
// 真实元素抢悬停，鼠标一进组件就乱跳。这里把高度写死，就只构建一次。
float ScrollArea(core::dsl::Ui& ui, const std::string& id, float x, float y, float w, float h,
                 float contentH, eui::Signal<float>& offSignal,
                 const std::function<void(core::dsl::Ui&, float)>& body) {
    const float maxOff = std::max(0.0f, contentH - h);
    const float off = std::clamp(offSignal.get(), 0.0f, maxOff);
    const float bodyW = std::max(Px(120.0f), w - Px(16.0f));   // 右侧留给自绘滚动条
    ui.stack(id)
        .x(x).y(y).size(w, h)
        .clip()
        .scrollState(id, off, maxOff, Px(52.0f))
        .onScrollOffsetChanged([&offSignal](float v) { offSignal.set(v); })
        .content([&] {
            ui.stack(id + ".content")
                .size(bodyW, contentH)
                .scrollContentFrom(id)   // 让子树跟着偏移，并修正命中判定
                .content([&] { body(ui, bodyW); })
                .build();
        })
        .build();
    return off;
}

// 自己画的滚动条（框架内置的那条在本项目主题下不显示）
void ScrollThumb(core::dsl::Ui& ui, const char* id, float x, float y, float w, float viewH,
                 float contentH, float off) {
    if (contentH <= viewH) return;
    const float barW = Px(6.0f);
    const float barX = x + w - barW;
    const float maxOff = contentH - viewH;
    const float thumbH = std::max(Px(48.0f), viewH * (viewH / contentH));
    const float thumbY = y + (viewH - thumbH) * (std::clamp(off, 0.0f, maxOff) / maxOff);
    ui.rect(std::string(id) + ".track")
        .x(barX).y(y).size(barW, viewH)
        .color(components::theme::withOpacity(g_theme.border, 0.35f))
        .radius(barW * 0.5f).build();
    ui.rect(std::string(id) + ".thumb")
        .x(barX).y(thumbY).size(barW, thumbH)
        .color(components::theme::withOpacity(g_theme.textMut, 0.75f))
        .radius(barW * 0.5f).build();
}

} // namespace app
