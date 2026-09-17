// 测试程序：验证 自启动（计划任务·最高权限）开关 API。
// 说明：创建 Highest 任务需要提权环境——非提权运行时 AutostartEnable 返回
// NeedElevation（这是设计行为，不是失败），测试输出中会明确区分。
#include "../src/win/autostart.h"

#include <cstdio>
#include <windows.h>

int main() {
    const bool before = AutostartEnabled();
    const AutostartResult on = AutostartEnable();
    const bool afterOn = AutostartEnabled();
    const bool off = AutostartDisable();
    const bool afterOff = AutostartEnabled();

    const char* r = on == AutostartResult::Ok ? "Ok"
                  : (on == AutostartResult::NeedElevation ? "NeedElevation" : "Failed");
    printf("before=%d enable=%s afterOn=%d disable=%d afterOff=%d\n",
           before, r, afterOn, off, afterOff);

    const char* verdict = "FAIL";
    if (on == AutostartResult::NeedElevation) {
        // 未提权：期望维持原状（不能把开关状态改坏）
        verdict = (afterOff == false) ? "PASS(need-elevation-path)" : "FAIL";
    } else if (on == AutostartResult::Ok) {
        verdict = (afterOn && off && !afterOff) ? "PASS" : "FAIL";
    }
    printf("verdict: %s\n", verdict);

    // 恢复测试前状态
    if (before) AutostartEnable();
    return 0;
}
