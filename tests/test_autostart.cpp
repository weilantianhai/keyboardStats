// 测试程序：验证 AutostartSet/AutostartEnabled 注册表逻辑
#include "../src/win/autostart.h"
#include <cstdio>

int main() {
    bool before = AutostartEnabled();
    AutostartSet(true);
    bool afterOn = AutostartEnabled();
    AutostartSet(false);
    bool afterOff = AutostartEnabled();
    printf("before=%d afterOn=%d afterOff=%d  => %s\n",
           before, afterOn, afterOff,
           (afterOn && !afterOff) ? "PASS" : "FAIL");
    return 0;
}
