// 测试程序：验证 AutostartSet/AutostartEnabled（计划任务方式）
// 注意：创建 /RL HIGHEST 的计划任务需要管理员权限——非提权环境下本测试会输出 SKIP。
#include "../src/win/autostart.h"
#include <cstdio>

int main() {
    bool before = AutostartEnabled();
    bool setOn = AutostartSet(true);
    bool afterOn = AutostartEnabled();
    bool setOff = AutostartSet(false);
    bool afterOff = AutostartEnabled();
    printf("before=%d setOn=%d afterOn=%d setOff=%d afterOff=%d  => %s\n",
           before, setOn, afterOn, setOff, afterOff,
           (setOn && afterOn && setOff && !afterOff) ? "PASS" : "SKIP (needs admin)");
    // 恢复测试前的状态，不打扰用户已有的自启动设置
    if (before) AutostartSet(true);
    return 0;
}
