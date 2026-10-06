// 页面绘制实现：头部 / 控制行 / 主页看板 / 热力图页 / 按键计数列表。
// 设置页、主题页、直方图页与两个弹窗已按页面拆到 pages_*.cpp；
// 跨页面共享的小构件与状态在 pages_common.h/.cpp；
// 热力着色的归一化（键盘/鼠标点击/滚轮/手柄四组峰值）在 heatnorm.cpp。
#include "pages.h"
#include "pages_common.h"
#include "theme.h"
#include "state.h"
#include "heatnorm.h"
#include "ui_util.h"
#include "fontscale.h"
#include "layout.h"
#include "components/components.h"
#include "timeutil.h"
#include "storage.h"
#include "padpref.h"
#include "hook.h"
#include "win/adminmode.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace app {

namespace {

float s_kbScale = 1.0f;      // 键盘尺寸系数（相对盒内自适应值）

float s_mouseScale = 1.0f;   // 鼠标尺寸系数

float s_padScale = 1.0f;     // 手柄尺寸系数

float s_split = 0.68f;       // 外盒 / 按键计数区 的边界位置（内容宽度的比例）

bool  s_layoutLoaded = false;

void EnsureLayoutPrefs() {
    if (s_layoutLoaded) return;
    s_layoutLoaded = true;
    s_kbScale    = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "kb", "1.0").c_str()), 0.6f, 1.6f);
    s_mouseScale = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "mouse", "1.0").c_str()), 0.6f, 1.8f);
    s_padScale   = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "pad", "1.0").c_str()), 0.6f, 1.8f);
    s_split      = std::clamp((float)atof(PrefGetValue(L"ui-layout.txt", "split", "0.68").c_str()), 0.30f, 0.90f);
}

void SaveLayoutPref(const char* key, float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", value);
    PrefSetValue(L"ui-layout.txt", key, buf);
}

// 叠放状态（带滞回，避免拖动尺寸时在并排/叠放间来回跳）
bool s_heatStacked = false;
// 按键筛选（键盘/鼠标/手柄/全部/分开）已统一为全局的 g_keyFilter（见 state.h）：
// 热力图着色、右侧按键计数列表、直方图页分布图三处共用同一个值。

// 内盒右下角外侧的尺寸指示器：按住左键拖动无极调节（松手存档）。
// 分配规则与"谁在被拖"无关——三方各取用户值，放不下时先键盘让位再压缩侧盒，
// 因此不需要区分调节方（早期版本用 driverId 决定谁保持尺寸，现已成为多余参数）。
void ResizeHandle(core::dsl::Ui& ui, const std::string& id, float x, float y,
                  float* value, float minValue, float maxValue, const char* prefKey) {
    const float s = Px(15.0f);
    ui.rect(id)
        .x(x).y(y).size(s, s)
        .color(g_theme.panelHi)
        .radius(Px(4.0f))
        .border(1.0f, g_theme.border)
        .states(g_theme.panelHi, g_theme.panelActive, g_theme.selected)
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .onDrag([value, minValue, maxValue](auto& e) {
            const float delta = (float)(e.deltaX + e.deltaY);
            if (delta == 0.0f) return;
            *value = std::clamp(*value * (1.0f + delta / 260.0f), minValue, maxValue);
            app::requestUpdate();
        })
        .onRelease([value, prefKey](auto&, auto&) { SaveLayoutPref(prefKey, *value); })
        .build();
    for (int i = 0; i < 3; ++i) {   // 抓握纹
        ui.rect(id + ".g" + std::to_string(i))
            .x(x + s * 0.26f + (float)i * s * 0.19f).y(y + s * 0.3f)
            .size(Px(1.5f), s * 0.4f)
            .color(g_theme.textMut)
            .radius(Px(1.0f))
            .build();
    }
}

// ── 实时按键状态（记录进程写入共享内存，GUI 只读）──
static bool KeyPressedNow(KeyCode vk) {
    const unsigned char* st = SharedKeyState();
    if (!st || vk >= (KeyCode)SharedKeySlotCount() || !st[vk]) return false;
    // state 前面是 uint32 tick：超过 1 秒没有新事件则视为过期（防 UP 丢失卡在按下态）
    const unsigned long tick = *reinterpret_cast<const volatile unsigned long*>(st - 4);
    return (GetTickCount() - tick) < 1000;
}

// 鼠标盒子：面板 + 机身，左右键/中键/侧键/滚轮（含上滑 ^ 下滑 v 键）按次数着色
void DrawMousePanel(core::dsl::Ui& ui, float x, float y, float w, float h) {
    const auto tk = CurrentTheme();
    ui.stack("mouse.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            // 鼠标盒子底
            ui.rect("mouse.box")
                .size(w, h)
                .color(g_theme.panelHi)
                .radius(Px(10.0f))
                .border(1.0f, g_theme.border)
                .build();
            const float pad = Px(13.0f);
            const float bodyW = w - pad * 2.0f;
            const float bodyH = h - pad * 2.0f;
            ui.rect("mouse.body")
                .x(pad).y(pad).size(bodyW, bodyH)
                .color(g_theme.idleKey)
                .radius(std::min(bodyW, bodyH) * 0.42f)
                .border(1.0f, g_theme.idleEdge)
                .build();

            auto button = [&](const std::string& id, KeyCode vk,
                              float bx, float by, float bw, float bh, float radiusK,
                              const std::string& glyph = std::string(), float glyphK = 0.5f) {
                const long c = (vk < kKeySlots) ? g_stats.counts[vk] : 0;
                // 归一化随筛选模式：键盘/鼠标/分开按各自组峰值，全部共用全局峰值
                double t = HeatNorm(vk, c);
                if (c > 0 && t < 0.08) t = 0.08;
                core::Color fill = HeatColor(t);
                // 实时按下反馈：物理按下时键面向主题色亮化（按住期间持续保持）
                const bool pressed = KeyPressedNow(vk);
                if (pressed) fill = core::mixColor(fill, g_theme.selected, 0.55f);
                ui.rect(id)
                    .x(bx).y(by).size(bw, bh)
                    .color(fill)
                    .radius(std::min(bw, bh) * radiusK)
                    .border(pressed ? 1.5f : 0.0f, g_theme.selected)
                    .states(fill,
                            core::mixColor(fill, Hex(0xFFFFFF), 0.15f),
                            core::mixColor(fill, Hex(0xFFFFFF), 0.25f))
                    .instantStates()
                    .transition(Motion())
                    .animate(core::AnimProperty::Color)
                    .build();
                if (!glyph.empty()) {
                    ui.text(id + ".glyph")
                        .x(bx).y(by).size(bw, bh)
                        .text(glyph)
                        .fontSize(std::min(bw, bh) * glyphK)
                        .lineHeight(bh)
                        .color(c > 0 || pressed ? Hex(0xFFFFFF) : g_theme.textMut)
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                }
                components::tooltip(ui, id + ".tip")
                    .theme(tk)
                    .source(id)
                    .value(Utf8(StatName(vk)) + " " + WithCommas(c) + " 次")
                    .anchor(bx + bw * 0.5f, by)
                    .bounds(w, h)
                    .style(components::TooltipStyle(tk))
                    .zIndex(300)
                    .build();
            };

            const float gap = Px(2.0f);
            const float topH = bodyH * 0.40f;
            const float halfW = (bodyW - gap) * 0.5f;
            button("mouse.l", kMouseLeft, pad, pad, halfW, topH, 0.30f);
            button("mouse.r", kMouseRight, pad + halfW + gap, pad, halfW, topH, 0.30f);

            const float midW = std::max(Px(11.0f), bodyW * 0.19f);
            const float midX = pad + (bodyW - midW) * 0.5f;
            const float keyH = std::max(Px(8.0f), bodyH * 0.13f);
            // 中键上方 = 滚轮上滑（^），下方 = 下滑（v）——中间不再有额外的方形键
            const float upTop = pad + Px(1.0f);
            button("mouse.wup", kWheelUp, midX, upTop, midW, keyH, 0.35f, "^", 0.7f);
            button("mouse.m", kMouseMiddle, midX, upTop + keyH + Px(2.0f), midW, bodyH * 0.16f, 0.40f);
            button("mouse.wdown", kWheelDown, midX, upTop + keyH + Px(2.0f) + bodyH * 0.16f + Px(2.0f),
                   midW, keyH, 0.35f, "v", 0.7f);
            // 侧键 X1 / X2（机身左侧两条）
            const float sideW = std::max(Px(9.0f), bodyW * 0.15f);
            button("mouse.x1", kMouseX1, pad + bodyW * 0.03f, pad + bodyH * 0.62f,
                   sideW, bodyH * 0.13f, 0.35f);
            button("mouse.x2", kMouseX2, pad + bodyW * 0.03f, pad + bodyH * 0.79f,
                   sideW, bodyH * 0.13f, 0.35f);
        })
        .build();
}

// 手柄盒子：Xbox 手柄示意（机身 = 整格圆角矩形，一层到底）。
//   · 机身铺满整格；ABXY、View·Menu、十字键四向、摇杆圈按**次数**热力着色，按下实时亮起
//   · 摇杆：大圈 = 内圈可移动范围，内圈按**实时模拟量**偏移（推杆即移动，松手回中）
//   · 机身左右内侧各一条竖直行程柱，实时显示 LT / RT 的**按下量**（模拟量，不计入统计）
//   · ABXY 菱形与右摇杆同轴（x=9.05），保证 A 键不被摇杆圈遮挡（见 layout.h）
void DrawPadPanel(core::dsl::Ui& ui, float x, float y, float w, float h) {
    const auto tk = CurrentTheme();
    ui.stack("pad.page")
        .x(x).y(y)
        .size(w, h)
        .content([&] {
            // 面板底板：整格铺一层，给两侧 LT/RT 行程柱一个着落。
            ui.rect("pad.sheet")
                .size(w, h)
                .color(g_theme.panel)
                .radius(Px(14.0f))
                .border(1.0f, g_theme.border)
                .build();

            // LT/RT 行程柱叠在机身左右内侧，因此手柄本体按"扣掉两条柱宽"的中间区域等比铺放。
            // 缩放按**内容的实际包围盒**计算（而不是整个 12×8 设计框）——否则设计框里
            // 未被使用的边距会白白占掉空间，手柄看起来又小又空。
            const float pad = Px(8.0f);
            const float innerW = std::max(Px(20.0f), w - pad * 2.0f);
            const float innerH = std::max(Px(20.0f), h - pad * 2.0f);

            // 内容包围盒 = 机身外接框 + 一点余量（避免描边被裁）。
            constexpr float kContentX0 = 0.45f, kContentX1 = 11.54f;
            constexpr float kContentY0 = 0.00f, kContentY1 = 7.95f;
            constexpr float kContentW = kContentX1 - kContentX0;
            constexpr float kContentH = kContentY1 - kContentY0;

            const float barW = std::clamp(innerW * 0.058f, Px(5.0f), Px(13.0f));
            const float barGap = std::max(Px(2.0f), innerW * 0.016f);
            const float midW = std::max(Px(24.0f), innerW - (barW + barGap) * 2.0f);
            const float s = std::min(midW / kContentW, innerH / kContentH);
            // 把内容包围盒的左上角映射到棋盘格内的居中位置
            const float ox = pad + barW + barGap + (midW - kContentW * s) * 0.5f - kContentX0 * s;
            const float oy = pad + (innerH - kContentH * s) * 0.5f - kContentY0 * s;
            const auto DX = [&](float v) { return ox + v * s; };
            const auto DY = [&](float v) { return oy + v * s; };
            const auto DS = [&](float v) { return v * s; };

            // 一个按键的绘制参数：次数 / 热力填充色（叠加按下高亮）/ 是否按下 / 是否"有内容"
            struct Paint { long count; core::Color fill; bool pressed; bool lit; };
            const auto paint = [&](KeyCode vk) {
                const long c = (vk < kKeySlots) ? g_stats.counts[vk] : 0;
                double t = HeatNorm(vk, c);          // 归一化随筛选模式（四组可各自求峰值）
                if (c > 0 && t < 0.08) t = 0.08;
                core::Color fill = HeatColor(t);
                const bool pressed = KeyPressedNow(vk);
                if (pressed) fill = core::mixColor(fill, g_theme.selected, 0.55f);
                return Paint{c, fill, pressed, c > 0 || pressed};
            };
            // 悬浮提示。行程条目（摇杆/扳机）的后缀跟"行程显示单位"开关走
            // （次 / mm），与计数列表同一口径；键鼠/手柄按键固定"次"。
            const auto tipText = [&](const std::string& id, const std::string& text,
                                     float ax, float ay) {
                components::tooltip(ui, id + ".tip")
                    .theme(tk)
                    .source(id)
                    .value(text)
                    .anchor(ax, ay)
                    .bounds(w, h)
                    .style(components::TooltipStyle(tk))
                    .zIndex(300)
                    .build();
            };
            const auto tip = [&](const std::string& id, KeyCode vk, long c,
                                 float ax, float ay) {
                tipText(id, Utf8(StatName(vk)) + " " + WithCommas(c) + " 次", ax, ay);
            };
            // 摇杆/扳机的**行程**悬浮：数据取自计数列表（FetchStats 已按单位折算）。
            // 悬停大圈/扳机柱时给的是"走了多远"，而不是 L3/R3 的按下次数——
            // 这两个量悬停时最容易混，分开呈现才不会让人对着 0 发呆。
            const auto travelText = [&](KeyCode analogVk) {
                for (const TopEntry& e : g_keyHist)
                    if (e.vk == analogVk)
                        return Utf8(StatName(analogVk)) + " 行程 " + WithCommas(e.count) +
                               " " + Utf8(PadUnitSuffixForCount(true));
                return Utf8(StatName(analogVk)) + " 行程 0 " +
                       Utf8(PadUnitSuffixForCount(true));
            };
            const auto capText = [&](const std::string& id, const wchar_t* cap,
                                     float bx, float by, float bw, float bh, bool lit) {
                if (!cap || !cap[0]) return;
                ui.text(id + ".cap")
                    .x(bx).y(by).size(bw, bh)
                    .text(Utf8(cap))
                    .fontSize(std::max(Px(7.0f), std::min(bw, bh) * 0.62f))
                    .lineHeight(bh)
                    .color(lit ? Hex(0xFFFFFF) : g_theme.textMut)
                    .horizontalAlign(core::HorizontalAlign::Center)
                    .verticalAlign(core::VerticalAlign::Center)
                    .build();
            };
            const auto polyPoints = [&](const PadPt* pts, int n) {
                std::vector<core::Vec2> out;
                out.reserve((size_t)n);
                for (int i = 0; i < n; ++i) out.push_back({pts[i].x * s, pts[i].y * s});
                return out;
            };

            // ① 机身本体：带握把的封闭多边形（layout.h 的 kPadBody）。
            //    早先是"整格圆角矩形"，看着像一块板；换成轮廓后才能认出是手柄。
            //
            //    配色说明：一开始用 panelHi，结果在浅色方案下与底板几乎融为一体——
            //    panelHi=#EEF2F8、panel=#FFFFFF 只差 17 级，白底上根本立不住轮廓。
            //    改用 idleKey(#E6EAF2) + idleEdge(#C9D2E0) 描边：
            //    与底板差 25 级、描边再差 29 级，深浅两套主题下轮廓都清晰。
            //    注意**不能**给这个多边形加 .radius()：49 个顶点里有不少短边，
            //    0.10 的圆角会让相邻圆角互相侵蚀、多边形自交，整块机身直接消失
            //    （实测：只剩左上角一小块）。直线段在 60px/单位的缩放下已经够平滑。
            ui.polygon("pad.body")
                .x(ox).y(oy).size(kPadLayoutW * s, kPadLayoutH * s)
                .points(polyPoints(kPadBody, kPadBodyPointCount))
                .color(g_theme.idleKey)
                .border(1.4f, g_theme.idleEdge)
                .build();
            // ② 十字键外框（十字形多边形；小圆角只做边缘柔化，不影响十字轮廓）
            ui.polygon("pad.dpad.frame")
                .x(ox).y(oy).size(kPadLayoutW * s, kPadLayoutH * s)
                .points(polyPoints(kPadDpadPoly, (int)(sizeof(kPadDpadPoly) / sizeof(kPadDpadPoly[0]))))
                .color(g_theme.idleKey)
                .border(1.0f, g_theme.idleEdge)
                .radius(DS(0.06f))
                .build();

            // ③ 按键（肩键矩形 + 圆形键 + 十字键四向热力块）
            for (const PadDef& b : kPadButtons) {
                const Paint kp = paint(b.vk);
                const std::string id = "pad." + std::to_string((int)b.vk);
                if (b.shape == PadShape::Poly) {
                    ui.polygon(id)
                        .x(ox).y(oy).size(kPadLayoutW * s, kPadLayoutH * s)
                        .points(polyPoints(b.pts, b.ptCount))
                        .color(kp.fill)
                        .border(kp.pressed ? 1.6f : 1.0f,
                                kp.pressed ? g_theme.selected : g_theme.idleEdge)
                        .radius(DS(0.10f))
                        .build();
                } else {
                    const float bw = DS(b.w), bh = DS(b.h);
                    const bool bumper = (b.vk == kPadLB || b.vk == kPadRB);
                    ui.rect(id)
                        .x(DX(b.x)).y(DY(b.y)).size(bw, bh)
                        // 必须用 .states() 注册交互态：tooltip 靠 hoverOpacityFrom
                        // 找 source 的 hoverBlend，而只有 interactive 元素才有；
                        // 之前只写了 .instantStates()，悬停命中根本不存在，
                        // 悬浮提示永远不弹（键盘键帽能弹正是因为它有 .states()）。
                        // hover 色轻微提亮给出悬停反馈；按下高亮已由 kp.pressed 处理。
                        .states(kp.fill,
                                core::mixColor(kp.fill, core::Color{1, 1, 1, 1}, 0.22f),
                                kp.fill)
                        .radius(b.shape == PadShape::Round ? std::min(bw, bh) * 0.5f
                              : bumper                      ? std::min(bw, bh) * 0.38f
                                                            : std::min(bw, bh) * 0.18f)
                        .border(kp.pressed ? 1.6f : 1.0f,
                                kp.pressed ? g_theme.selected : g_theme.idleEdge)
                        .instantStates()
                        .transition(Motion())
                        .animate(core::AnimProperty::Color)
                        .build();
                }
                capText(id, b.cap, DX(b.x), DY(b.y), DS(b.w), DS(b.h), kp.lit);
                tip(id, b.vk, kp.count, DX(b.x + b.w * 0.5f), DY(b.y));
            }

            // ④ 摇杆：大圈 = 可移动范围（L3/R3 的按下频率着色），内圈 = 实时位置
            SharedPadAnalog analog;
            bool analogOk = SharedPadAnalogRead(&analog);
            // 调试旁路（--padmirror=<文件>）：共享内存写入句柄独占，注入器进程
            // 常常抢不到，界面读到的就永远是记录进程发布的全 0，导致"数值变了
            // 界面跟不跟着动"测不出来。旁路改读文件，仅显式带参数时生效。
            if (DebugPadMirror(&analog)) analogOk = true;
            // 调试（--padseed）：没有真手柄（读不到，或读到的全是 0）时用一组固定值核对
            // 渲染——内圈偏移 + 两条行程柱；真手柄有非零值时照常用真值
            const bool allZero = analog.lx == 0.0f && analog.ly == 0.0f &&
                                 analog.rx == 0.0f && analog.ry == 0.0f &&
                                 analog.lt == 0.0f && analog.rt == 0.0f;
            if (DebugPadSeed() && (!analogOk || allZero)) {
                analog = SharedPadAnalog{0.45f, 0.35f, -0.40f, 0.30f, 0.72f, 0.22f};
                analogOk = true;
            }
            for (const PadStickDef& sd : kPadSticks) {
                const Paint kp = paint(sd.vk);
                const std::string id = "pad.stick." + std::to_string((int)sd.vk);
                const float ccx = DX(sd.cx), ccy = DY(sd.cy);
                const float range = DS(sd.range), dot = DS(sd.dot);
                ui.rect(id + ".range")
                    .x(ccx - range).y(ccy - range).size(range * 2.0f, range * 2.0f)
                    .states(kp.fill,
                            core::mixColor(kp.fill, core::Color{1, 1, 1, 1}, 0.22f),
                            kp.fill)   // 注册交互态：大圈悬停要弹行程提示
                    .radius(range)
                    .border(kp.pressed ? 2.0f : 1.2f,
                            kp.pressed ? g_theme.selected : g_theme.idleEdge)
                    .instantStates()
                    .transition(Motion())
                    .animate(core::AnimProperty::Color)
                    .build();
                // 内圈位置：模拟量 x 向右为正、y 向上为正（屏幕坐标相反，故取负）
                float ax = 0.0f, ay = 0.0f;
                if (analogOk) {
                    ax = (sd.axis == 0) ? analog.lx : analog.rx;
                    ay = -((sd.axis == 0) ? analog.ly : analog.ry);
                }
                ui.rect(id + ".dot")
                    .x(ccx + ax * std::max(0.0f, range - dot) - dot)
                    .y(ccy + ay * std::max(0.0f, range - dot) - dot)
                    .size(dot * 2.0f, dot * 2.0f)
                    .color(g_theme.selected)
                    .radius(dot)
                    .border(1.0f, g_theme.panelHi)
                    .build();
                tipText(id + ".travel",
                        travelText(sd.vk == kPadLS ? kPadStickL : kPadStickR),
                        ccx, ccy - range);
            }

            // ⑤ 扳机行程柱（左右各一条：轨道 + 自底向上的行程 + 标签）
            const float labelH = Px(13.0f);
            const float barTop = pad;
            const float barH = std::max(Px(18.0f), innerH - labelH - Px(4.0f));
            const auto triggerBar = [&](const std::string& id, const wchar_t* label,
                                        float value, float bx, KeyCode analogVk) {
                ui.rect(id + ".track")
                    .x(bx).y(barTop).size(barW, barH)
                    .states(g_theme.idleKey,
                            core::mixColor(g_theme.idleKey, core::Color{1, 1, 1, 1}, 0.22f),
                            g_theme.idleKey)   // 注册交互态：柱体悬停要弹行程提示
                    .radius(barW * 0.45f)
                    .border(1.0f, g_theme.idleEdge)
                    .build();
                const float fillH = barH * std::clamp(value, 0.0f, 1.0f);
                if (fillH > 0.6f) {
                    ui.rect(id + ".fill")
                        .x(bx).y(barTop + barH - fillH).size(barW, fillH)
                        .color(g_theme.selected)
                        .radius(barW * 0.45f)
                        .transition(Motion())
                        .instantStates()
                        .build();
                }
                ui.text(id + ".label")
                    .x(bx - Px(8.0f)).y(barTop + barH + Px(2.0f)).size(barW + Px(16.0f), labelH)
                    .text(Utf8(label))
                    .fontSize(std::max(Px(8.0f), barW * 0.9f))
                    .lineHeight(labelH)
                    .color(g_theme.textMut)
                    .horizontalAlign(core::HorizontalAlign::Center)
                    .build();
                // 悬停扳机柱：显示累计行程（与列表/单位开关同一口径）
                tipText(id + ".travel", travelText(analogVk),
                        bx + barW * 0.5f, barTop + barH);
            };
            triggerBar("pad.lt", L"LT", analogOk ? analog.lt : 0.0f, pad, kPadTrigL);
            triggerBar("pad.rt", L"RT", analogOk ? analog.rt : 0.0f, w - pad - barW, kPadTrigR);
        })
        .build();
}

// 外盒与按键计数区之间的边界：按住拖动无极调节（松手存档）
void SplitDivider(core::dsl::Ui& ui, float x, float y, float h, float contentX, float contentW) {
    const float w = Px(10.0f);
    ui.rect("heat.divider")
        .x(x).y(y).size(w, h)
        .color(g_theme.panelHi)
        .radius(Px(5.0f))
        .border(1.0f, g_theme.border)
        .states(g_theme.panelHi, g_theme.panelActive, g_theme.selected)
        .transition(Motion())
        .animate(core::AnimProperty::Color)
        .onDrag([contentX, contentW](auto& e) {
            const float delta = (float)e.deltaX;
            if (delta == 0.0f || contentW <= 0.0f) return;
            s_split = std::clamp(s_split + delta / contentW, 0.30f, 0.90f);
            app::requestUpdate();
        })
        .onRelease([](auto&, auto&) { SaveLayoutPref("split", s_split); })
        .build();
    for (int i = 0; i < 3; ++i) {   // 抓握纹
        ui.rect("heat.divider.g" + std::to_string(i))
            .x(x + w * 0.32f).y(y + h * 0.5f - Px(16.0f) + (float)i * Px(12.0f))
            .size(w * 0.36f, Px(2.0f))
            .color(g_theme.textMut)
            .radius(Px(1.0f))
            .build();
    }
}

void DrawKeycap(core::dsl::Ui& ui, int idx, float x, float y, float w, float h,
                long count, double t, float boundsW, float boundsH) {
    const auto tk = CurrentTheme();
    const std::string id = "key." + std::to_string(idx);
    const KeyCode vk = kKeys[idx].vk;
    core::Color fill = HeatColor(t);
    core::Color edge = count > 0 ? core::Color{0, 0, 0, 0} : g_theme.idleEdge;
    // 实时按下反馈：物理按下/按住时键面向主题色亮化并描边（与鼠标按键一致）
    const bool pressed = KeyPressedNow(vk);
    if (pressed) {
        fill = core::mixColor(fill, g_theme.selected, 0.55f);
        edge = g_theme.selected;
    }
    ui.rect(id)
        .x(x).y(y).size(w, h)
        .color(fill)
        .radius(std::min(8.0f, h * 0.28f))
        .border(pressed ? 1.5f : 1.0f, edge)
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
            .fontSize(std::min(24.0f, std::max(10.0f, h * 0.32f)))
            .lineHeight(std::min(24.0f, std::max(10.0f, h * 0.32f)))
            .color(count > 0 || pressed ? Hex(0xFFFFFF) : g_theme.textMut)
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }
    // 悬浮显示真实数据（与鼠标按键的悬浮一致）
    components::tooltip(ui, id + ".tip")
        .theme(tk)
        .source(id)
        .value(Utf8(StatName(vk)) + " " + WithCommas(count) + " 次")
        .anchor(x + w * 0.5f, y)
        .bounds(boundsW, boundsH)
        .style(components::TooltipStyle(tk))
        .zIndex(300)
        .build();
}

} // namespace

void DrawHeader(core::dsl::Ui& ui, float w) {
    ui.text("hd.title")
        .x(Px(28.0f)).y(Px(12.0f)).size(w - Px(200.0f), Px(46.0f))
        .text("KeyboardStats 键盘热力统计")
        .fontSize(Px(34.0f)).lineHeight(Px(40.0f))
        .color(g_theme.text)
        .build();
    std::string sub = "共 " + WithCommas(g_stats.total) + " 次按键 · " + g_rangeText;
    ui.text("hd.sub")
        .x(Px(28.0f)).y(Px(58.0f)).size(w - Px(200.0f), Px(28.0f))
        .text(sub)
        .fontSize(Px(19.0f)).lineHeight(Px(26.0f))
        .color(g_theme.textMut)
        .build();
    // 管理员模式状态：未提权时在副标题右侧给出一键提权入口
    // （管理员钩子是高完整性级别——管理员窗口、反作弊游戏（Valorant 等）内也能记录）
    if (RunningElevated()) {
        ui.text("hd.admin")
            .x(w - Px(560.0f)).y(Px(60.0f)).size(Px(230.0f), Px(24.0f))
            .text("● 管理员模式运行中（游戏内可记录）")
            .fontSize(Px(14.0f)).lineHeight(Px(24.0f))
            .color(Hex(0x22C55E))
            .build();
    } else {
        MiniButton(ui, "hd.admin", w - Px(560.0f), Px(52.0f), Px(230.0f), Px(32.0f),
                   "⚠ 未提权：游戏内无法记录 → 去开启", false, [] {
                       g_page = 2;                 // 跳转设置页
                       g_adminHighlight = true;
                       g_adminHighlightAt = GetTickCount64();
                       app::requestUpdate();
                   });
    }
    const float bh = Px(34.0f), by = Px(22.0f);
    MiniButton(ui, "hd.top10", w - 306.0f, by, 160.0f, bh,
               s_top10Open ? "隐藏按键列表" : "显示按键列表", false,
               [] { s_top10Open = !s_top10Open; app::requestUpdate(); });
    // 主题切换已独立成"主题"页，这里只留一个快捷入口
    MiniButton(ui, "hd.theme", w - 136.0f, by, 108.0f, bh, "主题", false, [] {
        g_page = 3;
        app::requestUpdate();
    });
}

void DrawControls(core::dsl::Ui& ui, const eui::Screen& screen) {
    const float y = ControlsY();
    const float h = Px(34.0f);

    // segmented 组件自身无定位方法，用带位置的 stack 容器承载
    // 横向位置/宽度不随缩放变化（受窗口宽度约束），只缩放高度
    ui.stack("ctrl.page")
        .x(28.0f).y(y).size(340.0f, h)
        .content([&] {
            components::segmented(ui, "seg.page")
                .size(340.0f, h)
                .items({"热力图", "直方图", "设置", "主题"})
                .selected(g_page)
                .theme(CurrentTheme())
                .transition(Motion())
                .onChange([](int v) { g_page = v; app::requestUpdate(); })
                .build();
        })
        .build();

    ui.stack("ctrl.range")
        .x(380.0f).y(y).size(360.0f, h)
        .content([&] {
            components::segmented(ui, "seg.range")
                .size(360.0f, h)
                .items({"今天", "7 天", "30 天", "全部"})
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
    // 只显示月-日：控件行横向空间有限（日期选择器内仍显示完整日期）
    auto ymdStr = [](uint32_t ymd) {
        if (!ymd) return std::string("选择日期");
        std::string s = Utf8(YmdToStr(ymd));
        return s.size() >= 10 ? s.substr(5) : s;
    };

    MiniButton(ui, "btn.from", 750.0f, y, 112.0f, h, "从 " + ymdStr(g_pendingFrom),
               false, [] { g_fromOpen.set(!g_fromOpen.get()); });
    MiniButton(ui, "btn.to", 870.0f, y, 112.0f, h, "至 " + ymdStr(g_pendingTo),
               false, [] { g_toOpen.set(!g_toOpen.get()); });
    MiniButton(ui, "btn.apply", 990.0f, y, 72.0f, h, "应用", true, [] {
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

// 外盒 / 列表几何（DrawHeatPage 与 DrawKeyList 共用，避免边界公式分叉）
struct HeatGeometry {
    float boardY = 0.0f, boardH = 0.0f;   // 主页看板（键鼠区上方横条）
    float boxX = 0.0f, boxY = 0.0f, boxW = 0.0f, boxH = 0.0f;
    float listX = 0.0f, listY = 0.0f, listW = 0.0f, listH = 0.0f;
    float contentX = 0.0f, contentW = 0.0f, gapX = 0.0f;
    bool  hasList = false;
};

HeatGeometry HeatGeom(const eui::Screen& screen) {
    EnsureLayoutPrefs();
    HeatGeometry g;
    g.contentX = Px(28.0f);
    g.contentW = screen.width - Px(56.0f);
    g.gapX = Px(22.0f);
    g.hasList = s_top10Open && screen.width >= Px(900.0f);
    const float listMinW = Px(210.0f);
    float boxW = g.hasList ? std::clamp(g.contentW * s_split, Px(380.0f),
                                       std::max(Px(380.0f), g.contentW - listMinW - g.gapX))
                           : g.contentW;
    boxW = std::max(boxW, Px(240.0f));
    g.boardY = ContentTop() + Px(4.0f);
    g.boardH = Px(64.0f);
    g.boxX = g.contentX;
    g.boxY = g.boardY + g.boardH + Px(10.0f);
    g.boxW = boxW;
    g.boxH = std::max(Px(160.0f), screen.height - g.boxY - Px(72.0f));
    g.listX = g.contentX + boxW + g.gapX;
    g.listY = g.boxY;
    g.listW = g.hasList ? std::max(Px(140.0f), g.contentX + g.contentW - g.listX) : 0.0f;
    g.listH = g.boxH;
    return g;
}

// ────────────────────────── 主页看板（键鼠区上方横条） ──────────────────────────

void DrawBoard(core::dsl::Ui& ui, const eui::Screen& screen) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;
    const HeatGeometry geo = HeatGeom(screen);
    const float x = geo.contentX, y = geo.boardY, w = geo.contentW, h = geo.boardH;

    ui.rect("board.bg")
        .x(x).y(y).size(w, h)
        .color(tk.surface)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 12.0f, 3.0f, 0.14f, 0.08f))
        .build();

    const TodayBreakdown td = StorageTodayBreakdown();
    const StorageInfo info = StorageDescribe();
    const long activeDays = StorageActiveDayCount();
    const long usedDays = info.firstYmd ? DayDiff(TodayLocal(), info.firstYmd) + 1 : 0;
    // 活跃分数：公式与权重都在 padpref.h（唯一来源，界面说明引用的也是同一组常量）。
    // 摇杆/扳机按模拟量行程计分——它们没有"次数"，只有走了多远。
    const PadTravel tv = PadTravelToday();
    const double stickTrips = PadStickTripsFromTravel(tv.stickL + tv.stickR);
    const double triggerPresses = tv.trigL + tv.trigR;
    const long score = PadScore(td.keyboard, td.mouseClicks, td.wheel, stickTrips, triggerPresses);

    struct Card { const char* label; std::string value; };
    const Card cards[7] = {
        {"活跃分数",  std::to_string(score)},
        {"今日键盘",  std::to_string(td.keyboard)},
        {"今日鼠标",  std::to_string(td.mouseClicks)},
        {"今日手柄",  std::to_string(td.pad)},
        {"滚轮格数",  std::to_string(td.wheel)},
        {"使用天数",  std::to_string(usedDays)},
        {"活跃天数",  std::to_string(activeDays)},
    };

    const int n = 7;
    const float pad = Px(14.0f);
    const float gapC = Px(10.0f);
    const float cw = (w - pad * 2.0f - gapC * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
        const float cx = x + pad + (cw + gapC) * i;
        ui.rect("board.c" + std::to_string(i))
            .x(cx).y(y + Px(9.0f)).size(cw, h - Px(18.0f))
            .color(g_theme.panel)
            .radius(Px(8.0f))
            .border(1.0f, i == 0 ? components::theme::withOpacity(g_theme.selected, 0.55f)
                                 : g_theme.border)
            .build();
        ui.text("board.l" + std::to_string(i))
            .x(cx + Px(10.0f)).y(y + Px(14.0f)).size(cw - Px(20.0f), Px(18.0f))
            .text(cards[i].label)
            .fontSize(m.typography.caption)
            .lineHeight(Px(18.0f))
            .color(g_theme.textMut)
            .build();
        ui.text("board.v" + std::to_string(i))
            .x(cx + Px(10.0f)).y(y + Px(32.0f)).size(cw - Px(20.0f), Px(24.0f))
            .text(cards[i].value)
            .fontSize(m.typography.title)
            .lineHeight(Px(24.0f))
            .color(i == 0 ? g_theme.selected : g_theme.text)
            .build();
    }
}

void DrawHeatPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const HeatGeometry geo = HeatGeom(screen);
    const float contentX = geo.contentX, contentW = geo.contentW, gapX = geo.gapX;
    const float boxX = geo.boxX, boxY = geo.boxY, boxW = geo.boxW, boxH = geo.boxH;
    const bool rail = geo.hasList;

    ui.rect("heat.box")
        .x(boxX).y(boxY).size(boxW, boxH)
        .color(g_theme.panel)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .build();

    // ── 盒内排布：键盘盒子 + 鼠标盒子 + 手柄盒子（并排；放不下时叠放）──
    const float pad = Px(14.0f);
    const float innerW = std::max(Px(60.0f), boxW - pad * 2.0f);
    const float innerH = std::max(Px(60.0f), boxH - pad * 2.0f);
    const float gapM = Px(20.0f);
    const float kbMinW = 24.0f * Px(9.0f);        // 每键最小 9px
    const float kbMaxW = 24.0f * Px(72.0f);
    const float mouseMinW = Px(56.0f);
    const float mouseMaxW = Px(200.0f);
    const float padMinW = Px(96.0f);              // 手柄盒要容纳机身 + 两条扳机行程柱
    const float padMaxW = Px(420.0f);              // 大窗口下模型要够大可读（高度上限另有限制）
    const float kMouseAspect = 1.62f;                     // 鼠标盒子 高/宽（竖长）
    const float kPadAspect = kPadLayoutW / kPadLayoutH;   // 手柄盒子 宽/高（横宽）

    // 参考尺寸：默认布局下的自然值，用户系数在此之上升降。
    // 两侧小盒的参考宽度由"键盘高度的几分之一"推出，保证与键盘的视觉比例协调
    const float kbRefW0 = std::clamp(std::min(innerW - Px(240.0f), innerH * 4.0f), kbMinW, kbMaxW);
    const float mouseRefW = std::clamp(kbRefW0 / 4.0f * 0.78f / kMouseAspect, mouseMinW, mouseMaxW);
    const float padRefW = std::clamp(kbRefW0 / 4.0f * 0.95f * kPadAspect, padMinW, padMaxW);
    const float kbRefW = std::clamp(
        std::min(innerW - mouseRefW - padRefW - gapM * 2.0f, innerH * 4.0f), kbMinW, kbMaxW);

    // 用户设定尺寸（各自最小/最大钳制）
    const float kbWantRaw = std::clamp(kbRefW * s_kbScale, kbMinW, kbMaxW);
    const float mouseWantRaw = std::clamp(mouseRefW * s_mouseScale, mouseMinW, mouseMaxW);
    const float padWantRaw = std::clamp(padRefW * s_padScale, padMinW, padMaxW);

    // 叠放判定带滞回（固定 12px 死区，避免拖动尺寸时来回跳）：
    //   进入 = 用户值放不下（且超出死区）；退出 = 能完整放下。
    // 这里**不能**用"留 10% 余量才退出"这种相对死区——三方的参考尺寸本身就是按
    // "正好铺满可用宽度"算出来的（见 kbRefW），一旦窗口短暂变窄触发叠放，就再也
    // 满足不了 10% 余量，会永久卡在叠放（本机实测：大窗口下仍显示叠放）。
    {
        const float wantSum = kbWantRaw + mouseWantRaw + padWantRaw + gapM * 2.0f;
        const float deadZone = Px(12.0f);
        if (!s_heatStacked) {
            if (wantSum > innerW + deadZone) s_heatStacked = true;
        } else if (wantSum <= innerW) {
            s_heatStacked = false;
        }
    }
    const bool stacked = s_heatStacked;

    // 尺寸求解：三方先各取用户值（侧盒受高度换算上限约束），
    // 并排放不下时先让键盘让位、再按比例压缩两个侧盒。
    float kbW = std::min(kbWantRaw, innerH * 4.0f);
    float mouseW = std::min(mouseWantRaw, innerH / kMouseAspect);
    float padW = std::min(padWantRaw, innerH * kPadAspect);

    if (stacked) {
        kbW = std::min(kbWantRaw, innerW);
        mouseW = std::min(mouseWantRaw, innerW);
        padW = std::min(padWantRaw, innerW);
        const float rowNeed = mouseW + padW + gapM;      // 第二行是鼠标 + 手柄并排
        if (rowNeed > innerW) {
            const float keep = std::max(0.0f, (innerW - gapM) / (mouseW + padW));
            mouseW = std::max(mouseMinW, mouseW * keep);
            padW = std::max(padMinW, padW * keep);
        }
    } else {
        // 侧盒视觉上限：盒子高度不超过键盘高度的 0.95 倍（避免侧盒比键盘还高）
        const float kbHeight = kbW / 4.0f;
        mouseW = std::clamp(mouseW, mouseMinW, std::min(mouseMaxW, kbHeight * 0.95f * kMouseAspect));
        // 手柄盒是横向的（宽:高 = 1.5:1），比键盘盒高一些才放得下完整机身，故上限放宽到 1.45 倍
        padW   = std::clamp(padW, padMinW, std::min(padMaxW, kbHeight * 1.45f * kPadAspect));
        float need = kbW + mouseW + padW + gapM * 2.0f;
        if (need > innerW) {                             // ① 键盘让位（不低于最小宽度）
            const float take = std::min(kbW - kbMinW, need - innerW);
            kbW -= take;
            need -= take;
        }
        if (need > innerW) {                             // ② 侧盒按比例压缩（不低于各自最小值）
            const float sideSum = mouseW + padW;
            const float keep = sideSum > 0.0f
                             ? std::max(0.0f, 1.0f - (need - innerW) / sideSum) : 1.0f;
            mouseW = std::max(mouseMinW, mouseW * keep);
            padW = std::max(padMinW, padW * keep);
        }
    }

    // 键盘高度由宽度决定（保持 24×6 的键位比例），再回算宽度使按键整除
    const float u = std::max(Px(6.0f), kbW / 24.0f);
    const float kbH = 6.0f * u;
    kbW = 24.0f * u;
    const float mouseH = mouseW * kMouseAspect;
    const float padH = padW / kPadAspect;

    const float sideRowW = mouseW + padW + gapM;      // 鼠标 + 手柄 一行（叠放时的第二行）
    const float sideRowH = std::max(mouseH, padH);
    const float contentH = stacked ? (kbH + gapM + sideRowH) : std::max({kbH, mouseH, padH});
    const bool needScroll = contentH > innerH;
    const float drawH = needScroll ? contentH : innerH;

    // 位置：组居中；并排=键盘左、鼠标中、手柄右；叠放=键盘在上、鼠标+手柄并排在下
    const float groupW = stacked ? std::max(kbW, sideRowW) : (kbW + gapM + sideRowW);
    const float gx = std::max(0.0f, (innerW - groupW) * 0.5f);
    const float kbX = stacked ? (gx + (groupW - kbW) * 0.5f) : gx;
    const float mouseX = stacked ? (gx + (groupW - sideRowW) * 0.5f) : (kbX + kbW + gapM);
    const float padX = mouseX + mouseW + gapM;
    const float mouseY = stacked ? (kbH + gapM + (sideRowH - mouseH) * 0.5f)
                                 : std::max(0.0f, (kbH - mouseH) * 0.5f);
    const float padY = stacked ? (kbH + gapM + (sideRowH - padH) * 0.5f)
                               : std::max(0.0f, (kbH - padH) * 0.5f);

    auto drawContent = [&](core::dsl::Ui& c) {
        const float gap = 2.0f;
        const float kbp = Px(10.0f);   // 键盘盒子内边距
        // 键盘盒子（大盒子内的子盒）
        c.rect("heat.kbbox")
            .x(kbX - kbp).y(-kbp)
            .size(kbW + kbp * 2.0f, kbH + kbp * 2.0f)
            .color(g_theme.panelHi)
            .radius(Px(10.0f))
            .border(1.0f, g_theme.border)
            .build();
        const int n = (int)(sizeof(kKeys) / sizeof(kKeys[0]));
        for (int i = 0; i < n; ++i) {
            const KeyDef& k = kKeys[i];
            long cnt = (k.vk < kKeySlots) ? g_stats.counts[k.vk] : 0;
            // 归一化随筛选模式（见 app::HeatNorm）：四组可各自独立求峰值
            double t = HeatNorm(k.vk, cnt);
            if (cnt > 0 && t < 0.08) t = 0.08;
            DrawKeycap(c, i, kbX + k.x * u + gap, k.y * u + gap,
                       k.w * u - gap * 2.0f, k.h * u - gap * 2.0f, cnt, t, innerW, drawH);
        }
        // 三个盒子各自的尺寸指示器（右下角外侧；钳制在内容范围内，避免被裁剪）
        const float hs = Px(17.0f);
        ResizeHandle(c, "kb.handle",
                     std::min(kbX + kbW + kbp + Px(5.0f), innerW - hs),
                     std::min(kbH + kbp + Px(5.0f), drawH - hs),
                     &s_kbScale, 0.6f, 1.6f, "kb");
        DrawMousePanel(c, mouseX, mouseY, mouseW, mouseH);
        ResizeHandle(c, "mouse.handle",
                     std::min(mouseX + mouseW + Px(5.0f), innerW - hs),
                     std::min(mouseY + mouseH + Px(5.0f), drawH - hs),
                     &s_mouseScale, 0.6f, 1.8f, "mouse");
        DrawPadPanel(c, padX, padY, padW, padH);
        ResizeHandle(c, "pad.handle",
                     std::min(padX + padW + Px(5.0f), innerW - hs),
                     std::min(padY + padH + Px(5.0f), drawH - hs),
                     &s_padScale, 0.6f, 1.8f, "pad");
    };

    if (needScroll) {
        components::scrollView(ui, "heat.scroll")
            .x(contentX + pad).y(boxY + pad)
            .size(innerW, innerH)
            .gap(0.0f)
            .step(Px(40.0f))
            .scrollbarWidth(Px(9.0f))
            .scrollbarGap(Px(3.0f))
            .theme(CurrentTheme())
            .transition(Motion())
            .content([&](core::dsl::Ui& cui, float cw, float) {
                cui.stack("heat.content")
                    .size(cw, drawH)
                    .content([&] { drawContent(cui); })
                    .build();
            })
            .build();
    } else {
        ui.stack("heat.content")
            .x(contentX + pad).y(boxY + pad)
            .size(innerW, drawH)
            .content([&] { drawContent(ui); })
            .build();
    }

    // ── 边界（可拖动无极调节盒宽 / 列表宽）──
    if (rail) {
        SplitDivider(ui, contentX + boxW + gapX * 0.5f - Px(5.0f), boxY, boxH, contentX, contentW);
    }

    // ── 图例（盒下方居中）──
    // 反转模式下色块序列经 HeatColor 自动镜像（左=红=少值色，右=蓝=多值色），
    // "少/多"文字**保持不变**——它标注的是当前映射下的颜色位置，语义仍然成立。
    const bool inv = HeatInverted();
    const float ly = boxY + boxH + Px(18.0f);
    const float legendW = 8.0f * Px(22.0f) + Px(70.0f);
    const float lx = contentX + boxW * 0.5f - legendW * 0.5f;
    ui.text("legend.lo")
        .x(lx).y(ly - Px(4.0f)).size(Px(32.0f), Px(20.0f))
        .text("少").fontSize(Px(16.0f)).lineHeight(Px(20.0f))
        .color(g_theme.textMut).horizontalAlign(core::HorizontalAlign::Right)
        .build();
    for (int i = 0; i < 8; ++i) {
        ui.rect("legend.sw." + std::to_string(i))
            .x(lx + Px(36.0f) + i * Px(22.0f)).y(ly)
            .size(Px(20.0f), Px(12.0f))
            .color(HeatColor((i + 0.5) / 8.0))
            .radius(3.0f)
            .build();
    }
    ui.text("legend.hi")
        .x(lx + Px(40.0f) + 8 * Px(22.0f)).y(ly - Px(4.0f)).size(Px(26.0f), Px(20.0f))
        .text("多").fontSize(Px(16.0f)).lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();
    // 反转开关（图例右侧）：高频显低频色、低频显高频色
    MiniButton(ui, "legend.invert", lx + legendW + Px(24.0f), ly - Px(9.0f),
               Px(110.0f), Px(32.0f), inv ? "取消反转" : "反转颜色", false, [] {
                   SetHeatInverted(!HeatInverted());
               });
}

// 右侧按键列表：全部有记录的按键（含鼠标/滚轮）按次数降序，内容超出高度即自动出滚动条
// 位置与宽度由外盒/列表之间的边界（SplitDivider）决定
void DrawKeyList(core::dsl::Ui& ui, const eui::Screen& screen) {
    const HeatGeometry geo = HeatGeom(screen);
    if (!geo.hasList) return;

    const float x = geo.listX, y = geo.listY, w = geo.listW, h = geo.listH;

    ui.rect("list.panel")
        .x(x).y(y).size(w, h)
        .color(g_theme.panel)
        .radius(Px(12.0f))
        .border(1.0f, g_theme.border)
        .build();
    ui.text("list.title")
        .x(x + Px(16.0f)).y(y + Px(14.0f)).size(w - Px(96.0f), Px(26.0f))
        .text("按键计数")
        .fontSize(Px(20.0f)).lineHeight(Px(24.0f))
        .color(g_theme.text)
        .build();
    MiniButton(ui, "list.collapse", x + w - Px(76.0f), y + Px(10.0f), Px(60.0f), Px(30.0f),
               "收起", false, [] { s_top10Open = false; app::requestUpdate(); });

    // 按键筛选：与热力图着色、直方图页共用同一个全局值（切换即时生效）
    const float filtY = y + Px(48.0f);
    const float filtW = std::max(Px(120.0f), w - Px(24.0f));
    const bool showFilter = filtW >= Px(232.0f);   // 面板过窄时隐藏（5 档需要更宽），保持"全部"
    if (showFilter) {
        ui.stack("list.filter")
            .x(x + Px(12.0f)).y(filtY).size(filtW, Px(32.0f))
            .content([&] {
                components::segmented(ui, "seg.listfilter")
                    .size(filtW, Px(32.0f))
                    .items({"键盘", "鼠标", "手柄", "全部", "分开"})
                    .selected(g_keyFilter)
                    .theme(CurrentTheme())
                    .transition(Motion())
                    .onChange([](int v) { g_keyFilter = v; app::requestUpdate(); })
                    .build();
            })
            .build();
    }

    const float listX = x + Px(12.0f);
    const float listY = filtY + (showFilter ? Px(40.0f) : Px(2.0f));
    const float listW = w - Px(24.0f);
    const float listH = std::max(Px(60.0f), y + h - Px(12.0f) - listY);
    const float rowH = Px(30.0f);

    components::scrollView(ui, "list.scroll")
        .x(listX).y(listY)
        .size(listW, listH)
        .gap(Px(4.0f))
        .step(rowH)
        .scrollbarWidth(Px(8.0f))
        .scrollbarGap(Px(4.0f))
        .theme(CurrentTheme())
        .transition(Motion())
        .content([&](core::dsl::Ui& cui, float contentW, float) {
            // 依筛选构建行：0=键盘 1=鼠标（含滚轮）2=手柄 3=全部 4=分开（分组分段，段内降序）
            struct Row { const TopEntry* e; std::string header; };
            std::vector<Row> rows;
            auto pushDesc = [&](KeyGroup g) {
                for (int k = (int)g_keyHist.size() - 1; k >= 0; --k) {
                    const TopEntry& e = g_keyHist[(size_t)k];
                    if (e.count <= 0 || e.group != g) continue;
                    rows.push_back({&e, std::string()});
                }
            };
            auto pushAll = [&] {
                for (int k = (int)g_keyHist.size() - 1; k >= 0; --k) {
                    const TopEntry& e = g_keyHist[(size_t)k];
                    if (e.count <= 0) continue;
                    rows.push_back({&e, std::string()});
                }
            };
            switch (g_keyFilter) {
                case 4:   // 分开：四个分组各一段，段内各自降序（与热力分组一一对应）
                    rows.push_back({nullptr, "键盘"});  pushDesc(KeyGroup::Keyboard);
                    rows.push_back({nullptr, "鼠标"});  pushDesc(KeyGroup::MouseButton);
                    rows.push_back({nullptr, "滚轮"});  pushDesc(KeyGroup::Wheel);
                    rows.push_back({nullptr, "手柄"});  pushDesc(KeyGroup::Gamepad);
                    break;
                case 3:   pushAll(); break;
                case 1:   pushDesc(KeyGroup::MouseButton);
                          pushDesc(KeyGroup::Wheel); break;
                case 2:   pushDesc(KeyGroup::Gamepad); break;
                default:  pushDesc(KeyGroup::Keyboard); break;
            }

            if (rows.empty()) {
                const char* hint = (g_keyFilter == 2) ? "暂无手柄记录（未连接手柄，或该时段没用过）"
                                 : (g_keyFilter == 0) ? "暂无键盘记录"
                                 : (g_keyFilter == 1) ? "暂无鼠标记录"
                                 :                      "暂无数据，去打几个字吧";
                cui.text("list.empty")
                    .size(contentW, Px(24.0f))
                    .text(hint)
                    .fontSize(Px(14.0f)).lineHeight(Px(20.0f))
                    .color(g_theme.textMut)
                    .build();
                return;
            }
            int rank = 0;
            for (size_t k = 0; k < rows.size(); ++k) {
                const Row& r = rows[k];
                const std::string id = "list.row." + std::to_string(k);
                if (r.e == nullptr) {                       // 分区标题
                    cui.text(id + ".header")
                        .size(contentW, Px(24.0f))
                        .text(r.header)
                        .fontSize(Px(15.0f)).lineHeight(Px(22.0f))
                        .color(g_theme.textMut)
                        .build();
                    rank = 0;
                    continue;
                }
                const TopEntry& e = *r.e;
                ++rank;
                cui.stack(id)
                    .size(contentW, rowH)
                    .content([&] {
                        cui.text(id + ".name")
                            .x(0.0f).y(0.0f).size(contentW * 0.62f, rowH)
                            .text(std::to_string(rank) + ". " + e.name)
                            .fontSize(Px(16.0f)).lineHeight(rowH)
                            .color(g_theme.text)
                            .build();
                        cui.text(id + ".count")
                            .x(contentW * 0.62f).y(0.0f).size(contentW * 0.38f, rowH)
                            // 摇杆/扳机的后缀跟"行程显示单位"走，键鼠固定"次"
                            .text(WithCommas(e.count) + " " + Utf8(PadUnitSuffixForCount(e.analog)))
                            .fontSize(Px(16.0f)).lineHeight(rowH)
                            .color(g_theme.textMut)
                            .horizontalAlign(core::HorizontalAlign::Right)
                            .build();
                        cui.rect(id + ".bar.bg")
                            .x(0.0f).y(rowH - Px(4.0f))
                            .size(contentW, 2.0f)
                            .color(g_theme.idleEdge)
                            .radius(1.0f)
                            .build();
                        cui.rect(id + ".bar")
                            .x(0.0f).y(rowH - Px(4.0f))
                            // 条宽与热力图同一套归一化：分开模式下四段各自满格，
                            // 其余模式也随筛选即时变化
                            .size(contentW * (float)HeatNorm(e.vk, e.count), 2.0f)
                            .color(g_theme.selected)
                            .radius(1.0f)
                            .build();
                    })
                    .build();
            }
        })
        .build();
}

} // namespace app
