// 手柄记录回归测试（数据层，不需要真的接手柄）。
//
// 覆盖六件事：
//   ① 手柄键码段（0x100 起）与键盘 VK 码、鼠标/滚轮伪键码不重叠；
//   ② 分组判定：手柄键全部归 Gamepad，键鼠各归各组；
//   ③ 分组归一化：五种筛选模式下四组峰值各自独立——键盘上万次不会把 14 个手柄
//      按键压成冷色，而"全部"模式仍是跨组共用全局峰值；
//   ④ 真实数据层往返：RecordKey(手柄码) → 落盘 → 全量重载 → QueryRange 读回，
//      验证码值 ≥ 256 的事件行能被正确解析与聚合；
//   ⑤ 模拟量行程（里程表语义）：增量累加、负值/零值过滤、落盘重载无损；
//   ⑥ 活跃分数口径：按键/点击/滚轮/摇杆/扳机各自权重与混合计分。
//
// 运行前设置 KEYBOARDSTATS_DIR 指向临时目录（不触碰真实数据）。
#include "../src/layout.h"
#include "../src/heatnorm.h"
#include "../src/storage.h"
#include "../src/padpref.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>

static int s_fail = 0;

static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) ++s_fail;
}

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

// 合成一份"第 i 个手柄键 = hi - i*step"的统计，用来验证分组归一化
static void FillCounts(long counts[kKeySlots], long keyboardMax, long padHi, long padStep) {
    for (int i = 0; i < kKeySlots; ++i) counts[i] = 0;
    counts['A'] = keyboardMax;
    counts[kMouseLeft] = 500;
    counts[kWheelUp] = 5000;
    for (int i = 0; i < kPadButtonCount; ++i) counts[kPadButtons[i].vk] = padHi - (long)i * padStep;
}

int main() {
    printf("== ① 键码段隔离 ==\n");
    check(kPadBase >= 256, "手柄段起点在键盘 VK 范围（0-255）之外");
    check(kPadEnd <= kKeySlots, "手柄段终点在 counts[] 槽位内");
    {
        // 用 kPadBits（采集层的全量清单：含摇杆按下）判定，避免遗漏不在面板控件表里的键
        bool noConflict = true;
        for (const PadBitDef& b : kPadBits) {
            if (IsMouseKey(b.vk) || b.vk < kPadBase || b.vk >= kPadEnd) noConflict = false;
            if (StatName(b.vk) == nullptr) noConflict = false;
            if (KeyGroupOf(b.vk) != KeyGroup::Gamepad) noConflict = false;
        }
        check(noConflict, "每个手柄键（含摇杆按下）都在 0x100 段、有名称、归 Gamepad 组");
    }
    {
        // 摇杆/扳机的虚拟键码：为让"按键计数"列表显示它们而存在（计数 = 行程折算的等效次数）。
        // 关键红线：它们**绝不能**出现在 kPadBits 或 kPadButtons 里——
        //   · 进了 kPadBits 会被当成按键上升沿，凭空多出按键计数；
        //   · 进了 kPadButtons 会在面板上画出一个不存在的按钮。
        const KeyCode analogs[] = {kPadStickL, kPadStickR, kPadTrigL, kPadTrigR};
        bool ok = true;
        for (KeyCode vk : analogs) {
            if (vk < kPadBase || vk >= kPadEnd) ok = false;
            if (StatName(vk) == nullptr) ok = false;
            if (KeyGroupOf(vk) != KeyGroup::Gamepad) ok = false;
            if (!IsPadAnalogKey(vk)) ok = false;
            for (const PadBitDef& b : kPadBits) if (b.vk == vk) ok = false;
            for (int i = 0; i < kPadButtonCount; ++i)
                if (kPadButtons[i].vk == vk) ok = false;
        }
        check(ok, "摇杆/扳机虚拟键码在手柄段内、有名称、归 Gamepad 组");
        check(!IsPadAnalogKey(kPadA) && !IsPadAnalogKey(kPadLS) && !IsPadAnalogKey('A'),
              "真实按键不被误判为模拟量键");
        // 与真实按键键码不冲突
        bool distinct = true;
        for (KeyCode a : analogs)
            for (KeyCode b : analogs) if (a != b && a == b) distinct = false;
        for (KeyCode a : analogs) if (a == kPadLS || a == kPadRS) distinct = false;
        check(distinct, "模拟量键码互不相同，且不与摇杆按下(L3/R3)重合");
    }

    printf("== ② 分组判定 ==\n");
    {
        bool allPad = true;
        for (const PadBitDef& b : kPadBits) if (KeyGroupOf(b.vk) != KeyGroup::Gamepad) allPad = false;
        check(allPad, "全部手柄键 → Gamepad 组");
        check(KeyGroupOf('A') == KeyGroup::Keyboard, "字母键 → Keyboard 组");
        check(KeyGroupOf(kMouseLeft) == KeyGroup::MouseButton, "鼠标左键 → MouseButton 组");
        check(KeyGroupOf(kWheelUp) == KeyGroup::Wheel, "滚轮上 → Wheel 组");
    }

    printf("== ③ 分组归一化（键盘 20000 次 / 手柄 900 递减）==\n");
    {
        static long counts[kKeySlots];
        FillCounts(counts, 20000, 900, 60);
        app::HeatMaximaFromCounts(counts);
        check(app::g_maxKey == 20000, "全局峰值 = 键盘峰值");
        check(app::g_maxKeyboard == 20000, "键盘组峰值");
        check(app::g_maxGamepad == 900, "手柄组峰值");
        check(app::g_maxMouseClick == 500, "鼠标点击组峰值");
        check(app::g_maxWheel == 5000, "滚轮组峰值");

        const KeyCode padTop = kPadButtons[0].vk;
        const KeyCode padLast = kPadButtons[kPadButtonCount - 1].vk;

        app::g_keyFilter = 0;   // 键盘
        check(Near(app::HeatNorm('A', 20000), 1.0), "键盘模式：键盘键满格");
        check(Near(app::HeatNorm(padTop, 900), 0.0), "键盘模式：手柄键不着色");

        app::g_keyFilter = 1;   // 鼠标
        check(Near(app::HeatNorm(kMouseLeft, 500), 1.0), "鼠标模式：鼠标键按本组峰值满格");
        check(Near(app::HeatNorm(kWheelUp, 5000), 1.0), "鼠标模式：滚轮按滚轮组峰值满格");
        check(Near(app::HeatNorm(padTop, 900), 0.0), "鼠标模式：手柄键不着色");

        app::g_keyFilter = 2;   // 手柄
        check(Near(app::HeatNorm(padTop, 900), 1.0), "手柄模式：手柄最常用键满格");
        check(app::HeatNorm(padLast, 900 - (kPadButtonCount - 1) * 60) > 0.0,
              "手柄模式：手柄最冷键仍有颜色（组内对比度拉满）");
        check(Near(app::HeatNorm('A', 20000), 0.0), "手柄模式：键盘键不着色");

        app::g_keyFilter = 3;   // 全部（跨组对比）
        check(Near(app::HeatNorm(padTop, 900), 900.0 / 20000.0),
              "全部模式：手柄键按全局峰值（会明显偏冷，属预期）");
        check(Near(app::HeatNorm('A', 20000), 1.0), "全部模式：键盘键满格");

        app::g_keyFilter = 4;   // 分开（四组各自独立）
        check(Near(app::HeatNorm(padTop, 900), 1.0), "分开模式：手柄键按本组峰值满格");
        check(Near(app::HeatNorm('A', 20000), 1.0), "分开模式：键盘键按本组峰值满格");
        check(Near(app::HeatNorm(kMouseLeft, 500), 1.0), "分开模式：鼠标键按本组峰值满格");
        check(Near(app::HeatNorm(kWheelUp, 5000), 1.0), "分开模式：滚轮按本组峰值满格");
    }

    printf("== ④ 数据层往返（RecordKey → 落盘 → 重载 → QueryRange）==\n");
    {
        StorageInit();
        // 用显式键码（不依赖面板控件表的下标，表结构变动也不会误取越界项）
        const KeyCode padA = kPadA, padLB = kPadLB, padRight = kPadRight;
        // 先取基线：临时目录里可能残留上次运行的记录，断言用增量而不是绝对值
        const RangeStats base = QueryRange(0, 0, 0);
        const TodayBreakdown baseTd = StorageTodayBreakdown();
        const long baseA = base.counts[padA], baseLB = base.counts[padLB];
        const long baseRight = base.counts[padRight], baseB = base.counts[kPadB];

        RecordKey(padA);
        RecordKey(padA);
        RecordKey(padLB);
        RecordKey(padRight);
        RecordKey('A');                       // 键盘事件混在一起，确认互不干扰
        StorageFlushNow();                    // 落盘
        StorageReloadFull();                  // 从文件全量重读（验证 ≥256 的码值能被解析）

        const RangeStats rs = QueryRange(0, 0, 0);   // 今天
        check(rs.counts[padA] == baseA + 2, "手柄 A 增 2 次（JSONL 往返无损）");
        check(rs.counts[padLB] == baseLB + 1, "手柄 LB 增 1 次");
        check(rs.counts[padRight] == baseRight + 1, "十字键右增 1 次");
        check(rs.counts[kPadB] == baseB, "未按的手柄键计数不变");

        const TodayBreakdown td = StorageTodayBreakdown();
        check(td.pad == baseTd.pad + 4, "今日拆分：手柄 +4 次（不计入键盘/鼠标）");
        check(td.keyboard == baseTd.keyboard + 1, "今日拆分：键盘 +1 次");
    }

    printf("== ⑤ 手柄模拟量行程（里程表）与活跃分数 ==\n");
    {
        // 行程是累计量，先取基线再断言增量（临时目录可能有上次运行的残留）
        const PadTravel base = PadTravelToday();

        // 一个采样周期内：左摇杆走 1.2、右摇杆 0.8、左扳机 0.5、右扳机 0.25
        PadTravelAdd(1.2f, 0.8f, 0.5f, 0.25f);
        // 纯 0 增量不改变任何东西（采集层在没有位移时会传 0）
        PadTravelAdd(0.0f, 0.0f, 0.0f, 0.0f);
        // 负值应被过滤（路程不可能为负），不能把累计量拉小
        PadTravelAdd(-5.0f, -5.0f, -5.0f, -5.0f);

        const PadTravel tv = PadTravelToday();
        check(Near(tv.stickL - tv.stickL, 0.0) && Near(tv.stickL - base.stickL, 1.2),
              "左摇杆行程累加 1.2（负值与 0 被过滤）");
        check(Near(tv.stickR - base.stickR, 0.8), "右摇杆行程累加 0.8");
        check(Near(tv.trigL - base.trigL, 0.5), "左扳机行程累加 0.5");
        check(Near(tv.trigR - base.trigR, 0.25), "右扳机行程累加 0.25");

        // 再次提交应继续累加（不是覆盖）——这是"里程表"语义的核心
        PadTravelAdd(1.0f, 0.0f, 0.0f, 0.0f);
        check(Near(PadTravelToday().stickL - base.stickL, 2.2), "第二次提交继续累加（2.2，非覆盖）");

        // 落盘 → 全量重载 → 值必须原样读回（同键多行相加）
        StorageFlushNow();
        StorageReloadFull();
        const PadTravel tv2 = PadTravelToday();
        check(Near(tv2.stickL - base.stickL, 2.2), "行程落盘重载后左摇杆仍为 2.2");
        check(Near(tv2.trigR - base.trigR, 0.25), "行程落盘重载后右扳机仍为 0.25");

        // 查询区间语义：全部区间不得小于今天
        const PadTravel all = PadTravelQuery(3, 0, 0);
        check(all.stickL >= tv2.stickL, "全部区间行程 ≥ 今日行程");
    }

    printf("== ⑥ 活跃分数口径 ==\n");
    {
        // 纯键盘：10 次按键 = 10 分
        check(app::PadScore(10, 0, 0, 0, 0) == 10, "10 次按键 = 10 分");
        // 鼠标点击与键盘等权
        check(app::PadScore(0, 7, 0, 0, 0) == 7, "7 次鼠标点击 = 7 分");
        // 滚轮 0.1：10 格 = 1 分（沿用既有设定）
        check(app::PadScore(0, 0, 10, 0, 0) == 1, "10 格滚轮 = 1 分");
        // 摇杆 0.3：一次满推往返（= 2 个半径的行程 → 1 次 trip）= 0.3 分
        check(Near(app::PadStickTripsFromTravel(2.0), 1.0), "行程 2.0 = 1 次满推往返");
        check(app::PadScore(0, 0, 0, app::PadStickTripsFromTravel(2.0), 0) == 0,
              "摇杆满推往返 1 次 < 1 分（0.3 分取整为 0）");
        // 10 次 × 0.3 = 3.0 → 3 分（取能凑整的量，便于用户自己验算）
        check(app::PadScore(0, 0, 0, app::PadStickTripsFromTravel(20.0), 0) == 3,
              "摇杆满推往返 10 次 = 3 分（0.3/次）");
        // 扳机 0.5：两次满按才抵一次按键
        check(app::PadScore(0, 0, 0, 0, 2) == 1, "扳机满按 2 次 = 1 分（0.5/次）");
        check(app::PadScore(0, 0, 0, 0, 4) == 2, "扳机满按 4 次 = 2 分");
        // 混合：3 键(3) + 2 点击(2) + 20 格滚轮(2) + 10 次摇杆往返(3) + 4 次扳机(2) = 12
        check(app::PadScore(3, 2, 20, 10.0, 4) == 12, "混合计分 3+2+2+3+2 = 12 分");
    }

    printf("== ⑥b 行程显示单位换算 ==\n");
    {
        using namespace app;
        // count：原样返回等效次数
        check(Near(PadStickToDisplay(3.0, PadUnit::Count), 3.0), "count 档：摇杆 3 次 → 3");
        check(Near(PadTriggerToDisplay(3.0, PadUnit::Count), 3.0), "count 档：扳机 3 次 → 3");
        // mm：乘各自的毫米基准（摇杆一趟 60mm、扳机一趟 10mm）
        check(Near(PadStickToDisplay(3.0, PadUnit::Mm), 180.0), "mm 档：摇杆 3 次 → 180mm");
        check(Near(PadTriggerToDisplay(3.0, PadUnit::Mm), 30.0), "mm 档：扳机 3 次 → 30mm");
        // 后缀：键鼠恒为"次"，行程条目跟着单位走
        check(std::wstring(PadUnitSuffixForCount(false)) == L"次", "键鼠条目后缀恒为「次」");
        check(std::wstring(PadUnitSuffixForCount(true)) == std::wstring(PadUnitSuffix(PadUnitGet())),
              "行程条目后缀跟随当前单位开关");
        check(std::wstring(PadUnitSuffix(PadUnit::Count)) == L"次" &&
              std::wstring(PadUnitSuffix(PadUnit::Mm)) == L"mm",
              "两个单位的后缀分别是「次」与「mm」");
        // id 往返：写进偏好再读回必须是同一个值（否则开关存了读不出来）
        check(PadUnitFromId(PadUnitId(PadUnit::Mm)) == PadUnit::Mm, "mm 的 id 往返一致");
        check(PadUnitFromId(PadUnitId(PadUnit::Count)) == PadUnit::Count, "count 的 id 往返一致");
        check(PadUnitFromId("乱写的") == PadUnit::Count, "未知 id 回落到 count");
    }

    printf("== ⑦ 手柄面板几何（设计坐标系 %.0f×%.0f）==\n", kPadLayoutW, kPadLayoutH);
    {
        // 所有顶点都必须在设计框内：越界会被裁掉（面板上出现半截图形）
        bool inBox = true;
        const auto checkPoly = [&](const PadPt* pts, int n) {
            for (int i = 0; i < n; ++i) {
                if (pts[i].x < 0.0f || pts[i].x > kPadLayoutW ||
                    pts[i].y < 0.0f || pts[i].y > kPadLayoutH) inBox = false;
            }
        };
        checkPoly(kPadBody, (int)(sizeof(kPadBody) / sizeof(kPadBody[0])));
        checkPoly(kPadLBPoly, (int)(sizeof(kPadLBPoly) / sizeof(kPadLBPoly[0])));
        checkPoly(kPadRBPoly, (int)(sizeof(kPadRBPoly) / sizeof(kPadRBPoly[0])));
        checkPoly(kPadDpadPoly, (int)(sizeof(kPadDpadPoly) / sizeof(kPadDpadPoly[0])));
        check(inBox, "机身/肩键/十字键轮廓的顶点都在设计坐标系内");

        bool rectsInBox = true, dupVk = false;
        for (int i = 0; i < kPadButtonCount; ++i) {
            const PadDef& b = kPadButtons[i];
            if (b.x < 0.0f || b.x + b.w > kPadLayoutW || b.y < 0.0f || b.y + b.h > kPadLayoutH)
                rectsInBox = false;
            if (b.shape == PadShape::Poly && (b.pts == nullptr || b.ptCount < 3)) rectsInBox = false;
            for (int j = i + 1; j < kPadButtonCount; ++j)
                if (kPadButtons[j].vk == b.vk) dupVk = true;
        }
        check(rectsInBox, "全部按键控件都在设计坐标系内且多边形顶点齐备");
        check(!dupVk, "按键控件的键码互不重复");
        check(kPadButtonCount + kPadStickCount == kPadBitCount,
              "面板控件数 = 采集按键数（14：不重不漏）");

        // 机身是**带握把的封闭多边形**（不再是圆角矩形）。这里要盯住三点：
        //   ① 顶点数适中——够画出握把，又不至于多到让边缘起锯齿；
        //   ② 外接框参数与顶点自洽（绘制时用它算缩放）；
        //   ③ 所有按键与摇杆圈都落在多边形**内部**。
        // ③ 是这次改动的要害：两个握把之间有个向下的缺口（底部中央内凹），
        //    下区的十字键与右摇杆离它很近，坐标一挪就可能被切掉半块。
        // 顶点数：真实 Xbox 皮肤的机身是贝塞尔插画，展平+抽稀后约 49 点才能保形；
        // 上限定 64 防止有人把整条原始曲线（200+ 点）直接贴进来（边缘会起锯齿）。
        check(kPadBodyPointCount >= 12 && kPadBodyPointCount <= 64,
              "机身多边形顶点数在合理区间（保形且不至于锯齿）");
        check(kPadBodyRectW > 0.0f && kPadBodyRectH > 0.0f, "机身外接框宽高为正");
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (int i = 0; i < kPadBodyPointCount; ++i) {
                x0 = std::min(x0, kPadBody[i].x); x1 = std::max(x1, kPadBody[i].x);
                y0 = std::min(y0, kPadBody[i].y); y1 = std::max(y1, kPadBody[i].y);
            }
            check(std::fabs(x0 - kPadBodyRectX) < 0.02f &&
                  std::fabs(y0 - kPadBodyRectY) < 0.02f &&
                  std::fabs((x1 - x0) - kPadBodyRectW) < 0.02f &&
                  std::fabs((y1 - y0) - kPadBodyRectH) < 0.02f,
                  "机身外接框与多边形顶点自洽");
        }
        {
            // 射线法判点在多边形内
            const auto inBody = [](float px, float py) {
                bool inside = false;
                for (int i = 0, j = kPadBodyPointCount - 1; i < kPadBodyPointCount; j = i++) {
                    const float xi = kPadBody[i].x, yi = kPadBody[i].y;
                    const float xj = kPadBody[j].x, yj = kPadBody[j].y;
                    if (((yi > py) != (yj > py)) &&
                        (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) inside = !inside;
                }
                return inside;
            };
            bool allIn = true;
            for (int i = 0; i < kPadButtonCount; ++i) {
                const PadDef& b = kPadButtons[i];
                // 肩键是**半嵌入**机身顶边的，上半截本来就在轮廓外，不参与判定
                if (b.vk == kPadLB || b.vk == kPadRB) continue;
                const float cs[4][2] = {{b.x, b.y}, {b.x + b.w, b.y},
                                        {b.x, b.y + b.h}, {b.x + b.w, b.y + b.h}};
                for (const auto& c : cs) if (!inBody(c[0], c[1])) allIn = false;
            }
            // 摇杆大圈：沿圆周采样 24 点（只看圆本身，内圈在大圈内不必再查）
            for (const PadStickDef& st : kPadSticks) {
                for (int k = 0; k < 24; ++k) {
                    const float a = (float)k / 24.0f * 6.2831853f;
                    if (!inBody(st.cx + std::cos(a) * st.range,
                                st.cy + std::sin(a) * st.range)) allIn = false;
                }
            }
            check(allIn, "按键与摇杆大圈全部落在机身轮廓内（不会被握把缺口切掉）");
        }

        // 关键回归：按键（矩形/圆）不得与摇杆大圈相交——曾出现右摇杆盖住 A 键的缺陷
        {
            bool noHit = true;
            const auto circleHitsRect = [](float cx, float cy, float r,
                                           float x, float y, float w, float h) {
                const float nx = std::clamp(cx, x, x + w);
                const float ny = std::clamp(cy, y, y + h);
                const float dx = cx - nx, dy = cy - ny;
                return dx * dx + dy * dy < r * r;
            };
            for (const PadStickDef& st : kPadSticks) {
                for (int i = 0; i < kPadButtonCount; ++i) {
                    const PadDef& b = kPadButtons[i];
                    if (circleHitsRect(st.cx, st.cy, st.range, b.x, b.y, b.w, b.h))
                        noHit = false;
                }
            }
            check(noHit, "摇杆大圈不与任何按键相交（A 键不再被右摇杆遮挡）");
        }

        // 两个摇杆的圆不得相交
        {
            const float dx = kPadSticks[0].cx - kPadSticks[1].cx;
            const float dy = kPadSticks[0].cy - kPadSticks[1].cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            check(dist > kPadSticks[0].range + kPadSticks[1].range,
                  "左右摇杆大圈互不相交");
        }

        // 摇杆：内圈必须能在大圈内自由移动（range ≥ dot），且大圈整体在设计框内
        bool stickOk = true, rangeFit = true;
        for (const PadStickDef& st : kPadSticks) {
            if (st.range < st.dot) stickOk = false;
            if (st.cx - st.range < 0.0f || st.cx + st.range > kPadLayoutW ||
                st.cy - st.range < 0.0f || st.cy + st.range > kPadLayoutH) rangeFit = false;
        }
        check(stickOk, "摇杆大圈半径 ≥ 内圈半径（大圈 = 内圈可移动范围）");
        check(rangeFit, "摇杆大圈完整落在设计坐标系内");
        check(kPadSticks[0].axis == 0 && kPadSticks[1].axis == 1,
              "左右摇杆分别绑定左/右模拟量轴");

        // 面板控件（12 个按键 + 2 个摇杆圈）应完整覆盖 14 个采集键
        check(kPadButtonCount == 12 && kPadStickCount == 2,
              "面板控件 = 12 个按键 + 2 个摇杆圈（与 kPadBits 的 14 项一一对应）");
    }

    printf("\n%s（失败 %d 项）\n", s_fail == 0 ? "PASS" : "FAIL", s_fail);
    return s_fail == 0 ? 0 : 1;
}
