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

void DrawCloseDialog(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!g_closeDialogOpen) return;
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    // 注意：整块弹窗**不要**设 zIndex —— MiniButton 内部不带 zIndex，
    // 一旦遮罩层用了更大的 zIndex，按钮就会跑到遮罩后面（看不见也点不到）。
    // 和"清除记录"那套弹窗一样，靠"后画覆盖先画"就够了。
    ui.rect("close.mask")
        .size(screen.width, screen.height)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
        .onClick([] { g_closeDialogOpen = false; app::requestUpdate(); })
        .build();
    const float dw = Px(520.0f), dh = Px(280.0f);
    const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
    ui.rect("close.dlg")
        .x(dx).y(dy).size(dw, dh)
        .color(tk.surface)
        .radius(Px(14.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
        .build();
    ui.text("close.title")
        .x(dx + Px(26.0f)).y(dy + Px(20.0f)).size(dw - Px(52.0f), Px(32.0f))
        .text("关闭窗口")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();
    // 手动换行：这一行太长会超出弹窗宽度
    ui.text("close.text")
        .x(dx + Px(26.0f)).y(dy + Px(64.0f)).size(dw - Px(52.0f), Px(76.0f))
        .text("关闭窗口后，后台记录程序会继续运行（内存占用极小），\n"
              "点任务栏托盘图标的「打开 KeyboardStats」随时唤起主窗口。\n"
              "「退出程序」会连后台记录一起退出。")
        .fontSize(m.typography.body)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();

    // 位置需要固定宽度的容器承载（组件自身无定位方法）
    ui.stack("close.check.row")
        .x(dx + Px(26.0f)).y(dy + Px(152.0f)).size(Px(300.0f), Px(34.0f))
        .content([&] {
            components::checkbox(ui, "close.check")
                .size(Px(280.0f), Px(34.0f))
                .checked(g_closeDontAsk)
                .text("不再提示，记住我的选择")
                .fontSize(m.typography.label)
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) { g_closeDontAsk = v; app::requestUpdate(); })
                .build();
        })
        .build();

    MiniButton(ui, "close.totray", dx + Px(26.0f), dy + dh - Px(64.0f),
               Px(196.0f), Px(44.0f), "关闭窗口", true,
               [] { CloseDialogDecide(false); });
    MiniButton(ui, "close.exit", dx + dw - Px(158.0f), dy + dh - Px(64.0f),
               Px(132.0f), Px(44.0f), "退出程序", false,
               [] { CloseDialogDecide(true); });
}

// ────────────────────────── 首次启动：自启动引导弹窗 ──────────────────────────

void DrawOnboardDialog(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!g_onboardOpen) return;
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;

    ui.rect("ob.mask")
        .size(screen.width, screen.height)
        .color(core::Color{0.0f, 0.0f, 0.0f, 0.50f})
        .build();   // 引导弹窗不允许点遮罩关闭（必须明确选择），也不做任何点击处理

    const float dw = Px(500.0f), dh = Px(300.0f);
    const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
    ui.rect("ob.dlg")
        .x(dx).y(dy).size(dw, dh)
        .color(tk.surface)
        .radius(Px(14.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
        .build();
    ui.text("ob.title")
        .x(dx + Px(26.0f)).y(dy + Px(22.0f)).size(dw - Px(52.0f), Px(32.0f))
        .text("开启开机自启动？")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();
    ui.text("ob.text")
        .x(dx + Px(26.0f)).y(dy + Px(66.0f)).size(dw - Px(52.0f), Px(110.0f))
        .text("开启后，电脑开机时会自动在后台记录键鼠使用，\n"
              "无需手动打开程序，安心无忧。\n"
              "后台记录只占约 2 MB 内存，托盘图标随时可\n"
              "以打开主窗口查看统计。")
        .fontSize(m.typography.body)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();
    ui.text("ob.hint")
        .x(dx + Px(26.0f)).y(dy + Px(170.0f)).size(dw - Px(52.0f), Px(22.0f))
        .text("之后也可以随时在「设置」页里开启或关闭。")
        .fontSize(m.typography.caption)
        .lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .build();

    MiniButton(ui, "ob.enable", dx + Px(26.0f), dy + dh - Px(64.0f),
               Px(196.0f), Px(44.0f), "开启自启动", true, [] {
                   const bool ok = AutostartSet(true);
                   PrefSetValue(L"ui-general.txt", "onboarded", "1");
                   g_onboardOpen = false;
                   app::requestUpdate();
                   (void)ok;   // 失败时设置页的开关状态仍准确（Enabled 查注册表）
               });
    MiniButton(ui, "ob.skip", dx + dw - Px(150.0f), dy + dh - Px(64.0f),
               Px(124.0f), Px(44.0f), "暂不", false, [] {
                   PrefSetValue(L"ui-general.txt", "onboarded", "1");
                   g_onboardOpen = false;
                   app::requestUpdate();
               });
}

} // namespace app
