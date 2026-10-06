// 测试程序：验证 自启动（计划任务·最高权限）开关 API。
//
// **非破坏性原则**：本测试绝不删除用户已经开着的自启动任务。
//   · 一开始就开着 → 只做只读校验（状态可重复读取），标记 PASS(preserved)，完全不碰任务；
//   · 一开始是关的 → 尝试开启：未提权时 API 应返回 NeedElevation（设计行为，不算失败）；
//     只有在"我们自己创建成功"的情况下才继续测关闭，并确保结束后回到原状（关）。
//
// 背景：早期版本无条件调用 AutostartDisable()，会把用户真实使用的计划任务删掉，
// 且在未提权时无法恢复——这是测试设计缺陷，已改为上面的非破坏式流程。
#include "../src/win/autostart.h"

#include <cstdio>
#include <windows.h>

static const char* ResultName(AutostartResult r) {
    switch (r) {
        case AutostartResult::Ok:            return "Ok";
        case AutostartResult::NeedElevation: return "NeedElevation";
        default:                             return "Failed";
    }
}

int main() {
    const bool before = AutostartEnabled();
    const bool recheck = AutostartEnabled();   // 二次读取应与首次一致（缓存路径）
    printf("before=%d recheck=%d\n", (int)before, (int)recheck);

    if (before) {
        // 用户环境本来就开着：只验证只读状态稳定，绝不增删任务
        printf("enable=<skipped: 检测到已有自启动任务，测试不做任何改动>\n");
        const bool stable = (before == recheck);
        printf("verdict: %s\n", stable ? "PASS(preserved)" : "FAIL");
        return stable ? 0 : 1;
    }

    const AutostartResult on = AutostartEnable();
    const bool afterOn = AutostartEnabled();
    printf("enable=%s afterOn=%d\n", ResultName(on), (int)afterOn);

    if (on == AutostartResult::NeedElevation) {
        // 未提权：不该也不能改动系统任务，期望状态保持"关"
        printf("verdict: %s\n", afterOn ? "FAIL" : "PASS(need-elevation-path)");
        return afterOn ? 1 : 0;
    }
    if (on != AutostartResult::Ok) {
        printf("verdict: FAIL（创建失败）\n");
        return 1;
    }

    // 任务由本测试创建 → 关闭它即可回到测试前的状态（关）
    const bool off = AutostartDisable();
    const bool afterOff = AutostartEnabled();
    printf("disable=%d afterOff=%d\n", (int)off, (int)afterOff);
    const bool ok = afterOn && off && !afterOff;
    printf("verdict: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
