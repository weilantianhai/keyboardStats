// 测试程序：验证 AutostartSet/AutostartEnabled 注册表逻辑
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
           (setOn && afterOn && setOff && !afterOff) ? "PASS" : "FAIL");
    // 恢复测试前的状态，不打扰用户已有的自启动设置
    if (before) AutostartSet(true);
    return 0;
}
