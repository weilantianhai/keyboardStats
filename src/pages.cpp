// 页面绘制实现：头部/控制行/热力图页/Top10/直方图页
#include "pages.h"
#include "theme.h"
#include "state.h"
#include "ui_util.h"
#include "layout.h"
#include "components/components.h"
#include "timeutil.h"

#include <algorithm>
#include <functional>
#include <string>

namespace app {

namespace {

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
        .radius(8.0f)
        .border(1.0f, accent ? core::Color{0, 0, 0, 0} : g_theme.border)
        .onClick(std::move(onClick))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();
    ui.text(id + ".t")
        .x(x).y(y).size(w, h)
        .text(label)
        .fontSize(15.0f)
        .lineHeight(16.0f)
        .color(accent ? Hex(0xFFFFFF) : g_theme.text)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

void DrawKeycap(core::dsl::Ui& ui, int idx, float x, float y, float w, float h,
                long count, double t) {
    const std::string id = "key." + std::to_string(idx);
    core::Color fill = HeatColor(t);
    core::Color edge = count > 0 ? core::Color{0, 0, 0, 0} : g_theme.idleEdge;
    ui.rect(id)
        .x(x).y(y).size(w, h)
        .color(fill)
        .radius(std::min(8.0f, h * 0.28f))
        .border(1.0f, edge)
        .states(fill,
                count > 0 ? core::mixColor(fill, Hex(0xFFFFFF), 0.10f) : g_theme.panelHi,
                count > 0 ? core::mixColor(fill, Hex(0xFFFFFF), 0.18f) : Hex(0x2A3245))
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .build();

    // 键帽文字：仅宽度足够的键（Space/Shift/Enter 等宽键 + 单键宽度 ≥34px）
    const wchar_t* cap = kKeys[idx].cap;
    if (kKeys[idx].w * 1.0f >= 2.0f || w >= 34.0f) {
        ui.text(id + ".t")
            .x(x).y(y).size(w, h)
            .text(Utf8(cap))
            .fontSize(std::min(20.0f, std::max(9.0f, h * 0.32f)))
            .lineHeight(std::min(20.0f, std::max(9.0f, h * 0.32f)))
            .color(count > 0 ? Hex(0xFFFFFF) : g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }
    // 计数：只标注 Space（唯一的 6.25u 超宽键），其余次数看 Top 10
    if (kKeys[idx].vk == 0x20 && kKeys[idx].w > 4.0f && count > 0) {
        ui.text(id + ".c")
            .x(x).y(y + h * 0.52f).size(w, h * 0.4f)
            .text(WithCommas(count))
            .fontSize(std::min(18.0f, std::max(12.0f, h * 0.26f)))
            .lineHeight(std::min(19.0f, std::max(13.0f, h * 0.28f)))
            .color(Hex(0x1E293B))
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Top)
            .build();
    }
}

} // namespace

void DrawHeader(core::dsl::Ui& ui, float w) {
    ui.text("hd.title")
        .x(28.0f).y(14.0f).size(w - 200.0f, 40.0f)
        .text("KeyboardStats 键盘热力统计")
        .fontSize(30.0f).lineHeight(36.0f)
        .color(g_theme.text)
        .build();
    std::string sub = "共 " + WithCommas(g_stats.total) + " 次按键 · " + g_rangeText;
    ui.text("hd.sub")
        .x(28.0f).y(58.0f).size(w - 200.0f, 24.0f)
        .text(sub)
        .fontSize(16.0f).lineHeight(22.0f)
        .color(g_theme.textMut)
        .build();
    MiniButton(ui, "hd.top10", w - 232.0f, 22.0f, 100.0f, 30.0f,
               s_top10Open ? "隐藏 Top 10" : "显示 Top 10", false,
               [] { s_top10Open = !s_top10Open; app::requestUpdate(); });
    MiniButton(ui, "hd.theme", w - 122.0f, 22.0f, 94.0f, 30.0f,
               g_lightMode ? "深色模式" : "浅色模式", false, ToggleTheme);
}

void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float y = 96.0f, h = 34.0f;

    // segmented 组件自身无定位方法，用带位置的 stack 容器承载
    ui.stack("ctrl.page")
        .x(28.0f).y(y).size(200.0f, h)
        .content([&] {
            components::segmented(ui, "seg.page")
                .size(200.0f, h)
                .items({"热力图", "直方图"})
                .selected(g_page)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_page = v; app::requestUpdate(); })
                .build();
        })
        .build();

    ui.stack("ctrl.range")
        .x(248.0f).y(y).size(380.0f, h)
        .content([&] {
            components::segmented(ui, "seg.range")
                .size(380.0f, h)
                .items({"今天", "最近 7 天", "最近 30 天", "全部"})
                .selected(g_rangeMode <= 3 ? g_rangeMode : 3)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_rangeMode = v; FetchStats(); app::requestUpdate(); })
                .build();
        })
        .build();

    uint32_t today = TodayLocal();
    if (!g_pendingFrom) g_pendingFrom = AddDays(today, -6);
    if (!g_pendingTo) g_pendingTo = today;
    auto ymdStr = [](uint32_t ymd) {
        if (!ymd) return std::string("选择日期");
        return Utf8(YmdToStr(ymd));
    };

    MiniButton(ui, "btn.from", 620.0f, y, 96.0f, h, "从 " + ymdStr(g_pendingFrom),
               false, [] { g_fromOpen.set(!g_fromOpen.get()); });
    MiniButton(ui, "btn.to", 724.0f, y, 96.0f, h, "至 " + ymdStr(g_pendingTo),
               false, [] { g_toOpen.set(!g_toOpen.get()); });
    MiniButton(ui, "btn.apply", 832.0f, y, 72.0f, h, "应用", true, [] {
        if (g_pendingFrom && g_pendingTo) {
            g_customFrom = g_pendingFrom;
            g_customTo = g_pendingTo;
            g_rangeMode = 4;
            FetchStats();
            app::requestUpdate();
        }
    });

    components::datePicker(ui, "dp.from")
        .screen(screen.width, screen.height)
        .date((int)(g_pendingFrom / 10000), (int)(g_pendingFrom / 100 % 100), (int)(g_pendingFrom % 100))
        .bindOpen(g_fromOpen)
        .theme(CurrentTheme())
        .zIndex(600)
        .onChange([](int y, int m, int d) { g_pendingFrom = YmdOf(y, m, d); app::requestUpdate(); })
        .build();
    components::datePicker(ui, "dp.to")
        .screen(screen.width, screen.height)
        .date((int)(g_pendingTo / 10000), (int)(g_pendingTo / 100 % 100), (int)(g_pendingTo % 100))
        .bindOpen(g_toOpen)
        .theme(CurrentTheme())
        .zIndex(600)
        .onChange([](int y, int m, int d) { g_pendingTo = YmdOf(y, m, d); app::requestUpdate(); })
        .build();
}

void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float top = 148.0f;
    // Top 10 侧栏开启且窗口足够宽时右侧预留 250px；窄窗口自动隐藏侧栏
    const bool rail = s_top10Open && screen.width >= 1000.0f;
    const float railW = rail ? 250.0f : 0.0f;
    const float availW = screen.width - railW - 56.0f;
    const float availH = screen.height - top - 70.0f;
    float u = std::min(availW / 24.0f, availH / 6.0f);
    u = std::clamp(u, 24.0f, 80.0f);   // 随窗口缩放，仅保留可用性下限
    const float kx = (screen.width - railW - u * 24.0f) * 0.5f;
    const float ky = top + 12.0f;
    const float gap = 2.0f;

    // 键盘面板底
    ui.rect("heat.panel")
        .x(kx - 14.0f).y(ky - 14.0f)
        .size(u * 24.0f + 28.0f, u * 6.0f + 28.0f)
        .color(g_theme.panel)
        .radius(12.0f)
        .border(1.0f, g_theme.border)
        .build();

    const int n = (int)(sizeof(kKeys) / sizeof(kKeys[0]));
    for (int i = 0; i < n; ++i) {
        const KeyDef& k = kKeys[i];
        long c = (k.vk < 256) ? g_stats.counts[k.vk] : 0;
        double t = g_maxKey > 1 ? (double)c / (double)g_maxKey : 0.0;
        if (c > 0 && t < 0.08) t = 0.08;   // 有按键但占比极小：给最低可见热度
        DrawKeycap(ui, i, kx + k.x * u + gap, ky + k.y * u + gap,
                   k.w * u - gap * 2.0f, k.h * u - gap * 2.0f, c, t);
    }

    // 图例：8 段渐变
    const float ly = ky + u * 6.0f + 22.0f;
    const float lx = kx + u * 24.0f - 8.0f * 22.0f - 46.0f;
    ui.text("legend.lo")
        .x(lx - 30.0f).y(ly - 3.0f).size(28.0f, 18.0f)
        .text("少").fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.textMut).horizontalAlign(core::HorizontalAlign::Right)
        .build();
    for (int i = 0; i < 8; ++i) {
        ui.rect("legend.sw." + std::to_string(i))
            .x(lx + i * 22.0f).y(ly)
            .size(20.0f, 12.0f)
            .color(HeatColor((i + 0.5) / 8.0))
            .radius(3.0f)
            .build();
    }
    ui.text("legend.hi")
        .x(lx + 8 * 22.0f + 4.0f).y(ly - 3.0f).size(24.0f, 18.0f)
        .text("多").fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.textMut)
        .build();
}

// Top 10 侧栏：右侧面板，头部带"收起"按钮（开启状态由 s_top10Open 控制）
void DrawTop10(core::dsl::Ui& ui, const eui::Screen& screen) {
    if (!(s_top10Open && screen.width >= 1000.0f)) return;

    const float x = screen.width - 250.0f;
    const float y = 148.0f;
    const float w = 226.0f;
    const float h = screen.height - y - 24.0f;

    ui.rect("top.panel")
        .x(x).y(y).size(w, h)
        .color(g_theme.panel)
        .radius(12.0f)
        .border(1.0f, g_theme.border)
        .build();
    ui.text("top.title")
        .x(x + 16.0f).y(y + 14.0f).size(w - 96.0f, 24.0f)
        .text("Top 10")
        .fontSize(17.0f).lineHeight(22.0f)
        .color(g_theme.text)
        .build();
    MiniButton(ui, "top.collapse", x + w - 76.0f, y + 10.0f, 60.0f, 26.0f,
               "收起", false, [] { s_top10Open = false; app::requestUpdate(); });

    float rowY = y + 46.0f;
    const float rowH = std::min(30.0f, (h - 60.0f) / 10.0f);
    const int n = (int)g_keyHist.size();
    const int start = std::max(0, n - 10);
    int rank = n - start;   // 升序数组的末尾即最大值，倒序输出
    for (int i = n - 1; i >= start; --i) {
        const TopEntry& e = g_keyHist[(size_t)i];
        std::string id = "top.row." + std::to_string(rank);
        ui.text(id + ".name")
            .x(x + 16.0f).y(rowY).size(w * 0.55f, rowH)
            .text(std::to_string(rank) + ". " + e.name)
            .fontSize(14.0f).lineHeight(rowH)
            .color(g_theme.text)
            .build();
        ui.text(id + ".count")
            .x(x + w * 0.55f).y(rowY).size(w - w * 0.55f - 16.0f, rowH)
            .text(WithCommas(e.count))
            .fontSize(14.0f).lineHeight(rowH)
            .color(g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Right)
            .build();
        ui.rect(id + ".bar.bg")
            .x(x + 16.0f).y(rowY + rowH - 4.0f)
            .size(w - 32.0f, 2.0f)
            .color(g_theme.idleEdge)
            .radius(1.0f)
            .build();
        ui.rect(id + ".bar")
            .x(x + 16.0f).y(rowY + rowH - 4.0f)
            .size((w - 32.0f) * e.frac, 2.0f)
            .color(g_theme.selected)
            .radius(1.0f)
            .transition(Motion())
            .animate(core::AnimProperty::Frame)
            .build();
        rowY += rowH + 4.0f;
        --rank;
    }
    if (g_keyHist.empty()) {
        ui.text("top.empty")
            .x(x + 16.0f).y(rowY).size(w - 32.0f, 24.0f)
            .text("暂无数据，去打几个字吧")
            .fontSize(14.0f).lineHeight(20.0f)
            .color(g_theme.textMut)
            .build();
    }
}

// 按键使用次数直方图：升序（左低右高），色条沿用热力渐变；
// 条上标注次数、条下标注键名（空间不足的自动跳过），面板右上角固定显示最多键。
// 宽度自适应：窄面板标题分两行、条形压缩铺满、标签按步长抽样。
void DrawKeyHist(core::dsl::Ui& ui, float x, float y, float w, float h) {
    ui.rect("keyhist.panel")
        .x(x).y(y).size(w, h)
        .color(g_theme.panel)
        .radius(12.0f)
        .border(1.0f, g_theme.border)
        .build();

    if (g_keyHist.empty()) {
        ui.text("keyhist.empty")
            .x(x + 16.0f).y(y + 40.0f).size(w - 32.0f, 24.0f)
            .text("暂无数据，去打几个字吧")
            .fontSize(14.0f).lineHeight(20.0f)
            .color(g_theme.textMut)
            .build();
        return;
    }

    const bool narrow = w < 900.0f;   // 标题行放不下"最多"注释时换行
    ui.text("keyhist.title")
        .x(x + 16.0f).y(y + 10.0f).size(narrow ? w - 32.0f : w * 0.55f, 20.0f)
        .text("按键使用次数分布（左 → 右 升序）")
        .fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.textMut)
        .build();
    // 面板右上角（窄面板时第二行右对齐）：最多键名 + 次数
    const TopEntry& maxE = g_keyHist.back();
    ui.text("keyhist.max")
        .x(narrow ? x + 16.0f : x + w * 0.5f)
        .y(narrow ? y + 28.0f : y + 10.0f)
        .size(narrow ? w - 32.0f : w * 0.5f - 16.0f, 20.0f)
        .text("最多：" + maxE.name + " " + WithCommas(maxE.count) + " 次")
        .fontSize(14.0f).lineHeight(18.0f)
        .color(g_theme.text)
        .horizontalAlign(core::HorizontalAlign::Right)
        .build();

    const float pad = 14.0f;
    const float barsTop = y + (narrow ? 52.0f : 38.0f);
    const float axisY = y + h - 30.0f;              // 条形底轴（下方留键名区）
    const float barsH = std::max(20.0f, axisY - barsTop);
    const float innerW = w - pad * 2.0f;
    const int n = (int)g_keyHist.size();
    const float barW = innerW / (float)n;           // 不设上限：条形始终从左铺到右
    const float startX = x + pad;
    // 标签抽样步长：条宽装不下标签时，每隔 k 根条标一次（k 使标签间距够宽）
    const int nameStride = std::max(1, (int)std::ceil(30.0f / barW));
    const int countStride = std::max(1, (int)std::ceil(24.0f / barW));

    for (int i = 0; i < n; ++i) {
        const TopEntry& e = g_keyHist[(size_t)i];
        const float frac = std::clamp(e.frac, 0.0f, 1.0f);
        const float bh = std::max(2.0f, barsH * frac);
        const float bx = startX + (float)i * barW;
        const float bw = std::max(1.5f, barW - 2.0f);
        const std::string id = "keyhist.bar." + std::to_string(i);
        ui.rect(id)
            .x(bx).y(axisY - bh)
            .size(bw, bh)
            .color(HeatColor(frac))   // 与键帽热力同一语义：蓝 → 黄 → 红
            .radius(2.0f)
            .transition(Motion())
            .animate(core::AnimProperty::Frame)
            .build();
        // 次数：条形足够高且水平放得下时标在条顶上方，否则按步长抽样
        bool cLabel = bh >= 16.0f && barW >= 12.0f;
        if (!cLabel && bh >= 16.0f && i % countStride == 0) {
            cLabel = (float)countStride * barW >= 20.0f;
        }
        if (cLabel) {
            ui.text(id + ".c")
                .x(bx - 6.0f).y(axisY - bh - 14.0f).size(bw + 12.0f, 12.0f)
                .text(WithCommas(e.count))
                .fontSize(9.0f).lineHeight(11.0f)
                .color(g_theme.textMut)
                .horizontalAlign(core::HorizontalAlign::Center)
                .build();
        }
        // 键名：条宽装得下才标；装不下时按步长抽样标注（估宽 ≈ 字符数 × 9px × 0.62）
        const float estW = (float)e.name.size() * 9.0f * 0.62f;
        bool nLabel = barW >= estW + 2.0f;
        if (!nLabel && i % nameStride == 0) {
            nLabel = (float)nameStride * barW >= estW + 2.0f;
        }
        if (nLabel) {
            ui.text(id + ".n")
                .x(bx - 8.0f).y(axisY + 4.0f).size(bw + 16.0f, 12.0f)
                .text(e.name)
                .fontSize(9.0f).lineHeight(11.0f)
                .color(g_theme.textMut)
                .horizontalAlign(core::HorizontalAlign::Center)
                .build();
        }
    }
}

void DrawHistPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float x = 28.0f, y = 148.0f;
    const float w = screen.width - 56.0f;
    const float totalH = screen.height - y - 24.0f;
    // 上：时间直方图（约 52%），下：按键使用次数直方图
    const float topH = totalH * 0.52f;
    const float histY = y + topH + 10.0f;
    const float histH = totalH - topH - 10.0f;

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
