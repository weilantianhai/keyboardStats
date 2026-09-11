// Split out of pages.cpp by page (pure code motion, behavior unchanged).
#include "pages_common.h"
#include "theme.h"
#include "state.h"
#include "ui_util.h"
#include "fontscale.h"
#include "layout.h"
#include "heatnorm.h"
#include "components/components.h"
#include "timeutil.h"
#include "storage.h"
#include "pref.h"
#include "win/filedialog.h"
#include "win/autostart.h"
#include "win/adminmode.h"
#include "hook.h"
#include "shellapi.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace app {

namespace {



} // namespace

// 按键使用次数直方图：升序（左低右高），几何/字号/交互与上方 barChart 保持一致
// （同一 plotX/plotY/plotW/plotH、4 条网格线、同圆角比例、hover 态 + 悬浮详情）。
// 键名仅在槽宽放得下时显示（窄窗口不显示），次数信息靠悬浮提示。
void DrawKeyHist(core::dsl::Ui& ui, float x, float y, float w, float h) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    ui.stack("keyhist.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            ui.rect("keyhist.bg")
                .size(w, h)
                .color(tk.surface)
                .radius(m.radius.section)
                .border(1.0f, components::theme::withOpacity(g_theme.border, 0.76f))
                .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
                .build();

            const float titleX = m.spacing.large;
            ui.text("keyhist.title")
                .x(titleX).y(m.typography.control)
                .size(std::max(0.0f, w * 0.55f), m.control.compact)
                .text("按键使用次数分布（左 → 右 升序）")
                .fontSize(m.typography.title)
                .lineHeight(m.typography.title + m.typography.lineGap)
                .color(g_theme.text)
                .build();

            if (g_keyHist.empty()) {
                ui.text("keyhist.empty")
                    .x(titleX).y(m.typography.control + m.typography.title + 12.0f)
                    .size(w - titleX * 2.0f, m.typography.body + 8.0f)
                    .text("暂无数据，去打几个字吧")
                    .fontSize(m.typography.body)
                    .lineHeight(m.typography.body + m.typography.lineGap)
                    .color(g_theme.textMut)
                    .build();
                return;
            }

            // 筛选：键盘 / 鼠标 / 全部 / 分开（分开=左键盘右鼠标，只改排序不改盒子）
            const float filterW = Px(360.0f), filterH = Px(34.0f);
            ui.stack("keyhist.filter")
                .x(w - titleX - filterW).y(m.typography.control - Px(2.0f)).size(filterW, filterH)
                .content([&] {
                    components::segmented(ui, "seg.keyfilter")
                        .size(filterW, filterH)
                        .items({"键盘", "鼠标", "全部", "分开"})
                        .selected(g_keyFilter)
                        .theme(tk)
                        .transition(Motion())
                        .onChange([](int v) { g_keyFilter = v; app::requestUpdate(); })
                        .build();
                })
                .build();

            // 右上角注解：最多键
            const TopEntry& maxE = g_keyHist.back();
            ui.text("keyhist.max")
                .x(titleX)
                .y(m.typography.control + m.typography.title + Px(4.0f))
                .size(std::max(0.0f, w - titleX * 2.0f), m.control.compact)
                .text("最多：" + maxE.name + " " + WithCommas(maxE.count) + " 次")
                .fontSize(m.typography.label)
                .lineHeight(m.typography.label + m.typography.lineGap)
                .color(g_theme.textMut)
                .build();

            // 绘图区几何与 barChart 相同
            const float plotX = 32.0f;
            const float plotY = 70.0f;
            const float plotW = std::max(1.0f, w - 64.0f);
            const float plotH = std::max(1.0f, h - 78.0f);   // 无横坐标文字，底部空间留给条形
            const float bottomY = plotY + plotH;
            const core::Color grid = components::theme::withOpacity(g_theme.border,
                                                                   tk.dark ? 0.38f : 0.36f);
            for (int line = 0; line < 4; ++line) {
                ui.rect("keyhist.grid." + std::to_string(line))
                    .x(plotX).y(plotY + (float)line * plotH / 3.0f)
                    .size(plotW, m.spacing.hairline)
                    .color(grid)
                    .build();
            }

            // 依筛选构建显示序列（全部为升序；分开 = 键盘在前、鼠标在后，仍是同一条序列）
            std::vector<const TopEntry*> items;
            for (const TopEntry& e : g_keyHist)                    // g_keyHist 本身按次数升序
                if (g_keyFilter == 0 && e.isMouse) continue;
                else if (g_keyFilter == 1 && !e.isMouse) continue;
                else if (g_keyFilter == 3 && e.isMouse) continue;  // 分开：先只放键盘
                else items.push_back(&e);
            if (g_keyFilter == 3) {
                for (const TopEntry& e : g_keyHist)
                    if (e.isMouse) items.push_back(&e);            // 再追加鼠标 → 排到最右
            }
            if (items.empty()) return;

            const int n = (int)items.size();
            const float slotW = plotW / (float)n;
            // 去掉横坐标文字后条形可以更宽一些
            const float barW = std::min(40.0f, std::max(2.0f, slotW * 0.70f));
            for (int i = 0; i < n; ++i) {
                const TopEntry& e = *items[(size_t)i];
                const float frac = std::clamp(e.frac, 0.0f, 1.0f);
                const float barH = std::max(8.0f, frac * plotH);
                const float bx = plotX + (float)i * slotW + (slotW - barW) * 0.5f;
                const float by = bottomY - barH;
                const std::string barId = "keyhist.bar." + std::to_string(i);
                const core::Color color = HeatColor(frac);   // 热力渐变：蓝 → 黄 → 红
                ui.rect(barId)
                    .x(bx).y(by).size(barW, barH)
                    .states(color,
                            core::mixColor(color, core::Color{1, 1, 1, 1}, 0.18f),
                            core::mixColor(color, core::Color{0, 0, 0, 1}, 0.12f))
                    .radius(std::min(m.radius.popup, barW * 0.34f))
                    .instantStates()
                    .transition(Motion())
                    .animate(core::AnimProperty::Frame | core::AnimProperty::Color)
                    .build();
                components::tooltip(ui, barId + ".tooltip")
                    .theme(tk)
                    .source(barId)
                    .value(e.name + " " + WithCommas(e.count) + " 次")
                    .anchor(bx + barW * 0.5f, by)
                    .bounds(w, h)
                    .style(components::TooltipStyle(tk))
                    .build();
            }
        })
        .build();
}

void DrawHistPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float x = Px(28.0f), y = ContentTop();
    const float w = screen.width - Px(56.0f);
    const float totalH = screen.height - y - Px(24.0f);
    // 上：时间直方图（约 52%），下：按键使用次数直方图
    const float topH = totalH * 0.52f;
    const float histY = y + topH + Px(10.0f);
    const float histH = totalH - topH - Px(10.0f);

    // 必须用定位 stack 包裹：barChart 构建器没有定位方法，
    // 直接 build 会从 (0,0) 画起并盖住顶部的页签控制行。
    ui.stack("hist.page")
        .x(x).y(y)
        .size(w, topH)
        .content([&] {
            components::barChart(ui, "hist")
                .size(w, topH)
                .title(g_rangeText)
                .values(g_barVals)
                .labels(g_barLabels)
                .theme(CurrentTheme())
                .transition(Motion())
                .build();
        })
        .build();

    DrawKeyHist(ui, x, histY, w, histH);
}

} // namespace app
