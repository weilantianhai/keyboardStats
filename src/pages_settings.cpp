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

// 记录管理状态（设置页）
StorageInfo s_recInfo;

double s_recInfoAt = 0.0;

std::string s_recMsg;

double s_recMsgAt = 0.0;

int s_clearConfirm = 0;   // 0=无 1=第一次确认 2=第二次确认

// 切换数据文件夹：原文件夹里发现数据文件时，先弹窗问是否一起搬走
std::wstring s_pendingDir;    // 待切换的目标文件夹（非空 = 显示确认框）

int s_pendingDirFiles = 0;    // 待搬运的数据文件个数

// 两个面板的高度按"内容需要"固定，不随窗口高度压缩：
// 之前按可用高度取比例，窗口一小面板就比内容矮，说明文字会和滑块叠在一起，
// 底部的按钮也会被窗口裁掉。现在改为固定内容高度 + 外层滚动视图。
constexpr float kFontPanelH = 440.0f;   // 含管理员开关行、说明文档行与框架署名

constexpr float kRecPanelH  = 276.0f;

// 字体设置面板：画在"滚动内容坐标系"里（原点 = 内容左上角，宽 = w）
static void DrawFontPanel(core::dsl::Ui& ui, float w, float y,
                          const components::theme::ThemeColorTokens& tk, float screenWidth) {
    const auto& m = tk.metrics;
    const float x = 0.0f;
    const float h = Px(kFontPanelH);

    ui.rect("set.panel")
        .x(x).y(y).size(w, h)
        .color(tk.surface)
        .radius(m.radius.section)
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
        .build();

    ui.text("set.title")
        .x(x + Px(24.0f)).y(y + Px(18.0f)).size(w - Px(48.0f), Px(30.0f))
        .text("设置")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();

    // ── 行 2：字体大小滑块（无极）──
    // ── 行 1.5：管理员权限（游戏等高完整性窗口内也能记录）──
    const float rowAdmin = y + Px(64.0f);
    // 闪烁引导：脉冲背景光 + 高亮边框，拨动开关或 15 秒后停止
    if (AdminHighlightActive()) {
        const double ph = std::sin(GetTickCount64() / 140.0);
        const float glow = 0.10f + 0.14f * float(0.5 + 0.5 * ph);
        ui.rect("set.admin.hl")
            .x(x + Px(12.0f)).y(rowAdmin - Px(10.0f)).size(w - Px(24.0f), Px(56.0f))
            .color(components::theme::withOpacity(g_theme.selected, glow))
            .radius(Px(10.0f))
            .border(1.5f, components::theme::withOpacity(g_theme.selected,
                     0.45f + 0.4f * float(0.5 + 0.5 * ph)))
            .build();
    }
    ui.text("set.admin.label")
        .x(x + Px(24.0f)).y(rowAdmin).size(w - Px(230.0f), Px(30.0f))
        .text("管理员模式（游戏内也可记录，重启程序生效）")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    ui.stack("set.admin.row")
        .x(x + w - Px(160.0f)).y(rowAdmin - Px(4.0f)).size(Px(136.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.admin")
                .size(Px(136.0f), Px(38.0f))
                .checked(AdminModeFlagged())
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) {
                    g_adminHighlight = false;   // 用户已操作，停止闪烁引导
                    if (!SetAdminModeFlagged(v)) {
                        s_recMsg = "设置失败（无法写入系统兼容性标记）";
                    } else if (v) {
                        s_recMsg = "将以管理员权限重启…";
                        s_recMsgAt = GetTickCount64() / 1000.0;
                        app::requestUpdate();
                        if (RelaunchAsAdmin()) {
                            Sleep(600);          // 等新提权实例接管
                            ExitAppNow();
                            return;
                        }
                        SetAdminModeFlagged(false);   // 用户取消 UAC → 回滚
                        s_recMsg = "已取消管理员授权，保持当前权限";
                    } else {
                        s_recMsg = "已关闭管理员模式，重启程序后生效";
                    }
                    s_recMsgAt = GetTickCount64() / 1000.0;
                    app::requestUpdate();
                })
                .build();
        })
        .build();

    // ── 行 2：开机自启动（只拉起记录程序 + 托盘图标，不带图形界面）──
    const float rowAuto = y + Px(124.0f);
    ui.text("set.autostart.label")
        .x(x + Px(24.0f)).y(rowAuto).size(w - Px(230.0f), Px(30.0f))
        .text("开机自启动（后台记录 + 托盘）")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    ui.stack("set.autostart.row")
        .x(x + w - Px(160.0f)).y(rowAuto - Px(4.0f)).size(Px(136.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.autostart")
                .size(Px(136.0f), Px(38.0f))
                .checked(AutostartEnabled())
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) {
                    const bool ok = AutostartSet(v);
                    s_recMsg = ok ? (v ? "已开启开机自启动（只启动记录程序）" : "已关闭开机自启动")
                                  : "设置失败（注册表写入被拒绝，请检查权限）";
                    s_recMsgAt = GetTickCount64() / 1000.0;
                    app::requestUpdate();
                })
                .build();
        })
        .build();

    const float row2 = y + Px(184.0f);
    const float sliderW = w - Px(48.0f) - Px(110.0f);
    const float shown = g_fontAuto ? AutoScaleForWidth(screenWidth) : g_fontCustom;
    ui.text("set.slider.label")
        .x(x + Px(24.0f)).y(row2 - Px(26.0f)).size(w * 0.6f, Px(24.0f))
        .text("字体大小")
        .fontSize(m.typography.label)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();
    ui.text("set.slider.value")
        .x(x + w - Px(134.0f)).y(row2 - Px(26.0f)).size(Px(110.0f), Px(24.0f))
        .text(FormatScale(shown))
        .fontSize(m.typography.label)
        .lineHeight(Px(24.0f))
        .color(g_theme.text)
        .horizontalAlign(core::HorizontalAlign::Right)
        .build();

    // 自动模式下不响应拖动，配色变暗以示不可调
    components::SliderStyle sl;
    sl.track = components::theme::withOpacity(g_theme.border, g_fontAuto ? 0.45f : 1.0f);
    sl.fill = g_fontAuto ? components::theme::withOpacity(g_theme.selected, 0.35f)
                         : g_theme.selected;
    sl.knob = g_fontAuto ? g_theme.textMut : g_theme.text;
    ui.stack("set.slider.row")
        .x(x + Px(24.0f)).y(row2 - Px(8.0f)).size(sliderW, Px(46.0f))
        .content([&] {
            components::slider(ui, "set.font")
                .size(sliderW, Px(46.0f))
                .value((shown - kFontScaleMin) / (kFontScaleMax - kFontScaleMin))
                .style(sl)
                .theme(tk)
                .transition(Motion())
                .onChange([](float v) {
                    if (g_fontAuto) return;   // 自动模式：滑块只读
                    // 立即生效（拖动跟手）；落盘由 TickFontScale 防抖
                    RequestFontCustom(kFontScaleMin + v * (kFontScaleMax - kFontScaleMin));
                    app::requestUpdate();
                })
                .build();
        })
        .build();

    ui.text("set.slider.min")
        .x(x + Px(24.0f)).y(row2 + Px(44.0f)).size(Px(60.0f), Px(22.0f))
        .text("小").fontSize(m.typography.caption).lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .build();
    ui.text("set.slider.max")
        .x(x + Px(24.0f) + sliderW - Px(60.0f)).y(row2 + Px(44.0f)).size(Px(60.0f), Px(22.0f))
        .text("大").fontSize(m.typography.caption).lineHeight(Px(22.0f))
        .color(g_theme.textMut)
        .horizontalAlign(core::HorizontalAlign::Right)
        .build();

    // ── 自动开关行（字体大小行下方）：开关 + 说明合一 ──
    const float rowAutoFont = y + Px(268.0f);
    ui.stack("set.auto.row")
        .x(x + Px(24.0f)).y(rowAutoFont - Px(4.0f)).size(Px(120.0f), Px(38.0f))
        .content([&] {
            components::toggleSwitch(ui, "set.auto")
                .size(Px(120.0f), Px(38.0f))
                .checked(g_fontAuto)
                .theme(tk)
                .transition(Motion())
                .onChange([](bool v) { SetFontAuto(v); app::requestUpdate(); })
                .build();
        })
        .build();
    ui.text("set.auto.label")
        .x(x + Px(158.0f)).y(rowAutoFont - Px(2.0f)).size(w - Px(200.0f), Px(42.0f))
        .text("自动：字号随窗口宽度缩放（宽窗口更大、窄窗口更小）。\n"
              "关闭后可拖动上方滑块统一调整界面全部字体。")
        .fontSize(m.typography.caption)
        .lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();

    // ── 行 4：说明文档（默认浏览器打开 GitHub 仓库的 README 页）──
    const float rowDoc = y + h - Px(52.0f);
    ui.text("set.doc.label")
        .x(x + Px(24.0f)).y(rowDoc + Px(4.0f)).size(w - Px(48.0f) - Px(160.0f), Px(30.0f))
        .text("说明文档：完整操作手册（在线页面）")
        .fontSize(m.typography.body)
        .lineHeight(Px(30.0f))
        .color(g_theme.text)
        .build();
    MiniButton(ui, "set.doc", x + w - Px(174.0f), rowDoc - Px(4.0f), Px(150.0f), Px(38.0f),
               "打开说明文档", false, [] {
                   ShellExecuteW(nullptr, L"open",
                                 L"https://github.com/weilantianhai/keyboardStats#readme",
                                 nullptr, nullptr, SW_SHOWNORMAL);
               });
    ui.text("set.doc.credit")
        .x(x + Px(24.0f)).y(rowDoc + Px(38.0f)).size(w - Px(48.0f), Px(20.0f))
        .text("本程序界面基于开源框架 EUI-NEO (Apache-2.0, github.com/sudoevolve/EUI-NEO) 构建")
        .fontSize(m.typography.caption)
        .lineHeight(Px(20.0f))
        .color(g_theme.textMut)
        .build();

}

// 记录管理面板（含数据文件夹/数据文件两行）：同样画在滚动内容坐标系里
static void DrawRecordPanel(core::dsl::Ui& ui, float w, float y,
                            const components::theme::ThemeColorTokens& tk) {
    const auto& m = tk.metrics;

    const double nowSec = GetTickCount64() / 1000.0;
    if (nowSec - s_recInfoAt >= 3.0) {   // 缓存统计，避免每帧扫描文件
        s_recInfoAt = nowSec;
        s_recInfo = StorageDescribe();
    }

    const float bx = 0.0f, bw = w;
    const float by = y;
    const float bh = Px(kRecPanelH);
    ui.rect("rec.panel")
        .x(bx).y(by).size(bw, bh)
        .color(tk.surface)
        .radius(m.radius.section)
        .border(1.0f, g_theme.border)
        .shadow(components::theme::shadow(tk, 18.0f, 4.0f, 0.20f, 0.10f))
        .build();
    ui.text("rec.title")
        .x(bx + Px(24.0f)).y(by + Px(18.0f)).size(bw - Px(48.0f), Px(30.0f))
        .text("记录管理")
        .fontSize(m.typography.title)
        .lineHeight(m.typography.title + m.typography.lineGap)
        .color(g_theme.text)
        .build();

    // 统计行：条数 / 文件数 / 时间范围 / 占用
    std::string stats = "暂无记录";
    if (s_recInfo.events > 0) {
        const std::string first = Utf8(YmdToStr(s_recInfo.firstYmd));
        const std::string last = Utf8(YmdToStr(s_recInfo.lastYmd));
        char sizeBuf[32];
        if (s_recInfo.bytes >= 1024 * 1024)
            snprintf(sizeBuf, sizeof sizeBuf, "%.1f MB", s_recInfo.bytes / 1048576.0);
        else
            snprintf(sizeBuf, sizeof sizeBuf, "%.0f KB", s_recInfo.bytes / 1024.0);
        stats = WithCommas(s_recInfo.events) + " 条事件 · " + std::to_string(s_recInfo.files) +
                " 个数据文件 · " + first + " ~ " + last + " · " + sizeBuf;
    }
    ui.text("rec.stats")
        .x(bx + Px(24.0f)).y(by + Px(54.0f)).size(bw - Px(48.0f), Px(26.0f))
        .text(stats)
        .fontSize(m.typography.label)
        .lineHeight(Px(24.0f))
        .color(g_theme.textMut)
        .build();

    // ────────────────── 数据位置（文件夹 / 文件）──────────────────
    // 路径可能很长，中间省略以保证一行放得下
    auto elide = [](const std::string& s, size_t keep) {
        if (s.size() <= keep) return s;
        return s.substr(0, keep / 2 - 1) + "…" + s.substr(s.size() - (keep / 2 - 2));
    };

    const float miniH = Px(32.0f), miniGap = Px(8.0f);
    const float right = bx + bw - Px(24.0f);
    const float wSecond = Px(58.0f), wFirst = Px(72.0f);   // 数据文件夹行：主操作 + 复位
    const float valueW = right - (wFirst + wSecond + miniGap * 2) - (bx + Px(24.0f) + Px(88.0f));
    const float folderY = by + Px(86.0f);
    const float fileY = by + Px(126.0f);
    // 数据文件行有 3 个按钮（新建/选择/自动），宽度按同样比例分配
    const float fNew = Px(58.0f), fPick = Px(58.0f), fAuto = Px(58.0f);
    const float fileValueW = right - (fNew + fPick + fAuto + miniGap * 2) - (bx + Px(24.0f) + Px(88.0f));

    // 目标文件夹里没有数据文件时直接切换；有则记下来弹确认框
    auto requestFolderChange = [&](const std::wstring& dir) {
        const int n = (int)DataFilesInFolder(DataFolderPath()).size();
        if (n == 0) {
            std::wstring err;
            if (StorageSetDataFolder(dir, false, &err, nullptr)) {
                FetchStats();
                s_recInfoAt = 0.0;
                s_recMsg = "数据文件夹已切换到 " + Utf8(dir);
            } else {
                s_recMsg = "切换失败：" + Utf8(err);
            }
            s_recMsgAt = GetTickCount64() / 1000.0;
            app::requestUpdate();
            return;
        }
        s_pendingDir = dir;
        s_pendingDirFiles = n;
        app::requestUpdate();
    };

    auto pathRow = [&](const char* id, float rowY, const char* label, const std::string& value,
                       float textW) {
        ui.text(std::string(id) + ".label")
            .x(bx + Px(24.0f)).y(rowY + Px(6.0f)).size(Px(88.0f), Px(24.0f))
            .text(label)
            .fontSize(m.typography.label)
            .lineHeight(Px(24.0f))
            .color(g_theme.textMut)
            .build();
        ui.text(std::string(id) + ".value")
            .x(bx + Px(24.0f) + Px(88.0f)).y(rowY + Px(6.0f)).size(textW, Px(24.0f))
            .text(value)
            .fontSize(m.typography.body)
            .lineHeight(Px(24.0f))
            .color(g_theme.text)
            .build();
    };

    pathRow("rec.folder", folderY, "数据文件夹",
            elide(Utf8(DataFolderPath()), (size_t)std::max(12.0f, valueW / Px(7.0f))), valueW);
    MiniButton(ui, "rec.folder.pick", right - (wFirst + wSecond + miniGap), folderY,
               wFirst, miniH, "更改", false, [requestFolderChange] {
        std::wstring dir;
        if (!PickFolder(L"选择数据文件夹", &dir)) return;
        requestFolderChange(dir);
    });
    MiniButton(ui, "rec.folder.def", right - wSecond, folderY, wSecond, miniH, "默认", false,
               [requestFolderChange] {
        requestFolderChange(DefaultDataFolder());
    });

    const std::wstring curFile = DataFileName();
    pathRow("rec.file", fileY, "数据文件",
            elide(curFile.empty() ? std::string("自动（按月 events-YYYYMM.jsonl）") : Utf8(curFile),
                  (size_t)std::max(12.0f, fileValueW / Px(7.0f))), fileValueW);
    MiniButton(ui, "rec.file.new", right - (fNew + fPick + fAuto + miniGap * 2), fileY,
               fNew, miniH, "新建", false, [] {
        std::wstring name, err;
        if (StorageCreateDataFile(&name, &err)) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已新建并切换到 " + Utf8(name);
        } else {
            s_recMsg = "新建失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.file.pick", right - (fPick + fAuto + miniGap), fileY,
               fPick, miniH, "选择", false, [] {
        std::wstring path;
        if (!PickOpenFile(L"选择数据文件（JSONL 事件文件）",
                          L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0", &path,
                          DataFolderPath())) return;   // 从数据文件夹打开，别让用户自己找
        std::wstring err;
        const long n = StorageAdoptJsonl(path, &err);
        if (n >= 0) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已切换到 " + Utf8(DataFileName()) + "（" + WithCommas(n) + " 条事件）";
        } else {
            s_recMsg = "切换失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.file.auto", right - fAuto, fileY, fAuto, miniH, "自动", false, [] {
        std::wstring err;
        if (StorageSetDataFile(L"", &err)) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已恢复按月自动数据文件";
        } else {
            s_recMsg = "恢复失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });

    // 操作按钮行
    const int btnCount = 5;
    const float btnGap = Px(10.0f);
    const float btnW = (bw - Px(48.0f) - btnGap * (btnCount - 1)) / (float)btnCount;
    const float btnY = by + Px(176.0f);
    const float btnH = Px(44.0f);
    auto btnX = [&](int i) { return bx + Px(24.0f) + (float)i * (btnW + btnGap); };

    MiniButton(ui, "rec.import", btnX(0), btnY, btnW, btnH, "转入文件", false, [] {
        std::wstring path;
        if (!PickOpenFile(L"转入数据文件（会移动到数据文件夹并切换）",
                          L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0", &path,
                          DataFolderPath())) return;
        std::wstring err;
        const long n = StorageAdoptJsonl(path, &err);
        if (n >= 0) {
            FetchStats();
            s_recInfoAt = 0.0;
            s_recMsg = "已转入并切换，共 " + WithCommas(n) + " 条事件";
        } else {
            s_recMsg = "转入失败：" + Utf8(err);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.expjsonl", btnX(1), btnY, btnW, btnH, "导出 JSONL", false, [] {
        std::wstring path;
        if (!PickSaveFile(L"导出记录（JSONL）", L"JSONL 事件文件\0*.jsonl\0所有文件\0*.*\0\0",
                          L"jsonl", L"keyboardstats-export.jsonl", &path,
                          DataFolderPath())) return;
        long n = 0;
        s_recMsg = StorageExportJsonl(path, &n) ? ("已导出 " + WithCommas(n) + " 条事件")
                                                : "导出失败（无法写入文件）";
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.expcsv", btnX(2), btnY, btnW, btnH, "导出 CSV", false, [] {
        std::wstring path;
        if (!PickSaveFile(L"导出记录（CSV）", L"CSV 表格\0*.csv\0所有文件\0*.*\0\0",
                          L"csv", L"keyboardstats-export.csv", &path,
                          DataFolderPath())) return;
        long n = 0;
        s_recMsg = StorageExportCsv(path, &n) ? ("已导出 " + WithCommas(n) + " 条事件")
                                              : "导出失败（无法写入文件）";
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.opendir", btnX(3), btnY, btnW, btnH, "打开数据目录", false, [] {
        // 明确打开"当前数据文件夹"，并把路径回显到提示行：
        // 之前数据文件夹默认在 exe 同级，打开后看着像打开了程序目录，容易误判。
        const std::wstring dir = DataFolderPath();
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
            CreateDirectoryW(dir.c_str(), nullptr);   // 还没建出来就先建，别静默失败
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr,
                                          SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            s_recMsg = "打开失败：" + Utf8(dir);
        } else {
            s_recMsg = "已打开 " + Utf8(dir);
        }
        s_recMsgAt = GetTickCount64() / 1000.0;
        app::requestUpdate();
    });
    MiniButton(ui, "rec.clear", btnX(4), btnY, btnW, btnH, "清除全部记录", true, [] {
        s_clearConfirm = 1;   // 第一步确认
        app::requestUpdate();
    });

    // 操作结果提示（显示 6 秒）：固定在按钮行下方，不再用面板底边反推
    if (!s_recMsg.empty() && nowSec - s_recMsgAt < 6.0) {
        ui.text("rec.msg")
            .x(bx + Px(24.0f)).y(by + Px(228.0f)).size(bw - Px(48.0f), Px(28.0f))
            .text(s_recMsg)
            .fontSize(m.typography.label)
            .lineHeight(Px(26.0f))
            .color(g_theme.text)
            .build();
    }
}

} // namespace

// 设置页：内容整体放进滚动视图，窗口再矮也不会重叠或截断
void DrawSettingsPage(core::dsl::Ui& ui, const eui::Screen& screen) {
    const auto tk = CurrentTheme();
    const auto& m = tk.metrics;
    const float x = Px(28.0f), y = ContentTop();
    const float w = std::min(screen.width - Px(56.0f), Px(760.0f));
    const float viewH = std::max(Px(120.0f), screen.height - y - Px(24.0f));
    const float gapY = Px(kPanelGap);

    const float contentH = Px(kFontPanelH) + gapY + Px(kRecPanelH);

    const float off = ScrollArea(ui, "set.scroll", x, y, w, viewH, contentH, s_settingsScroll,
                                 [&](core::dsl::Ui& su, float cw) {
                                     DrawFontPanel(su, cw, 0.0f, tk, screen.width);
                                     DrawRecordPanel(su, cw, Px(kFontPanelH) + gapY, tk);
                                 });
    ScrollThumb(ui, "set.scroll", x, y, w, viewH, contentH, off);

    // 清除记录：两步确认弹窗
    if (s_clearConfirm > 0) {
        ui.rect("rec.mask")
            .size(screen.width, screen.height)
            .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
            .onClick([] { s_clearConfirm = 0; app::requestUpdate(); })
            .build();
        const float dw = Px(470.0f), dh = Px(210.0f);
        const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
        ui.rect("rec.dlg")
            .x(dx).y(dy).size(dw, dh)
            .color(tk.surface)
            .radius(Px(14.0f))
            .border(1.0f, g_theme.border)
            .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
            .build();
        ui.text("rec.dlg.title")
            .x(dx + Px(26.0f)).y(dy + Px(22.0f)).size(dw - Px(52.0f), Px(34.0f))
            .text(s_clearConfirm == 1 ? "确认清除全部记录？" : "再次确认：数据将永久丢失")
            .fontSize(m.typography.title)
            .lineHeight(m.typography.title + m.typography.lineGap)
            .color(g_theme.text)
            .build();
        const std::string dlgText = (s_clearConfirm == 1)
            ? ("将删除 " + WithCommas(s_recInfo.events) + " 条事件记录及计数缓存，"
               "此操作不可撤销。\n如需保留，请先使用“导出 JSONL”备份。")
            : "这是最后一次确认：点击“确认清除”后，所有记录立即删除且无法恢复。";
        ui.text("rec.dlg.text")
            .x(dx + Px(26.0f)).y(dy + Px(74.0f)).size(dw - Px(52.0f), Px(70.0f))
            .text(dlgText)
            .fontSize(m.typography.body)
            .lineHeight(Px(28.0f))
            .color(g_theme.textMut)
            .build();
        MiniButton(ui, "rec.dlg.cancel", dx + dw - Px(250.0f), dy + dh - Px(64.0f),
                   Px(104.0f), Px(44.0f), "取消", false,
                   [] { s_clearConfirm = 0; app::requestUpdate(); });
        MiniButton(ui, "rec.dlg.ok", dx + dw - Px(136.0f), dy + dh - Px(64.0f),
                   Px(110.0f), Px(44.0f),
                   s_clearConfirm == 1 ? "继续" : "确认清除", true, [] {
                       if (s_clearConfirm == 1) {
                           s_clearConfirm = 2;   // 第二步确认
                       } else {
                           StorageClearAll();
                           FetchStats();
                           s_recInfoAt = 0.0;
                           s_clearConfirm = 0;
                           s_recMsg = "已清除全部记录";
                           s_recMsgAt = GetTickCount64() / 1000.0;
                       }
                       app::requestUpdate();
                   });
    }

    // 切换数据文件夹：原文件夹里有数据文件时，问是否一起搬走
    if (!s_pendingDir.empty()) {
        ui.rect("dir.mask")
            .size(screen.width, screen.height)
            .color(core::Color{0.0f, 0.0f, 0.0f, 0.45f})
            .onClick([] { s_pendingDir.clear(); app::requestUpdate(); })
            .build();
        const float dw = Px(520.0f), dh = Px(230.0f);
        const float dx = (screen.width - dw) * 0.5f, dy = (screen.height - dh) * 0.5f;
        ui.rect("dir.dlg")
            .x(dx).y(dy).size(dw, dh)
            .color(tk.surface)
            .radius(Px(14.0f))
            .border(1.0f, g_theme.border)
            .shadow(components::theme::shadow(tk, 28.0f, 8.0f, 0.28f, 0.16f))
            .build();
        ui.text("dir.dlg.title")
            .x(dx + Px(26.0f)).y(dy + Px(22.0f)).size(dw - Px(52.0f), Px(34.0f))
            .text("切换数据文件夹")
            .fontSize(m.typography.title)
            .lineHeight(m.typography.title + m.typography.lineGap)
            .color(g_theme.text)
            .build();
        const std::string dirDlgText =
            "新文件夹：" + Utf8(s_pendingDir) + "\n当前数据文件夹里有 " +
            std::to_string(s_pendingDirFiles) + " 个数据文件。是否把它们一起移动过去？";
        ui.text("dir.dlg.text")
            .x(dx + Px(26.0f)).y(dy + Px(74.0f)).size(dw - Px(52.0f), Px(70.0f))
            .text(dirDlgText)
            .fontSize(m.typography.body)
            .lineHeight(Px(28.0f))
            .color(g_theme.textMut)
            .build();
        MiniButton(ui, "dir.dlg.cancel", dx + Px(26.0f), dy + dh - Px(64.0f),
                   Px(96.0f), Px(44.0f), "取消", false,
                   [] { s_pendingDir.clear(); app::requestUpdate(); });
        MiniButton(ui, "dir.dlg.keep", dx + dw - Px(300.0f), dy + dh - Px(64.0f),
                   Px(120.0f), Px(44.0f), "仅切换", false, [] {
                       const std::wstring dir = s_pendingDir;
                       s_pendingDir.clear();
                       std::wstring err;
                       if (StorageSetDataFolder(dir, false, &err, nullptr)) {
                           FetchStats();
                           s_recInfoAt = 0.0;
                           s_recMsg = "已切换（原数据留在原文件夹）";
                       } else {
                           s_recMsg = "切换失败：" + Utf8(err);
                       }
                       s_recMsgAt = GetTickCount64() / 1000.0;
                       app::requestUpdate();
                   });
        MiniButton(ui, "dir.dlg.move", dx + dw - Px(170.0f), dy + dh - Px(64.0f),
                   Px(144.0f), Px(44.0f), "一起移动", true, [] {
                       const std::wstring dir = s_pendingDir;
                       s_pendingDir.clear();
                       std::wstring err;
                       FolderSwitchResult r;
                       if (StorageSetDataFolder(dir, true, &err, &r)) {
                           FetchStats();
                           s_recInfoAt = 0.0;
                           s_recMsg = "已切换，移动 " + std::to_string(r.moved) + " 个数据文件";
                           if (r.failed > 0)
                               s_recMsg += "，" + std::to_string(r.failed) + " 个失败（可能被占用）";
                       } else {
                           s_recMsg = "切换失败：" + Utf8(err);
                       }
                       s_recMsgAt = GetTickCount64() / 1000.0;
                       app::requestUpdate();
                   });
    }
}

// ────────────────────────── 主题页 ──────────────────────────

} // namespace app
