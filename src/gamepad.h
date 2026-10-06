#pragma once
// Xbox 手柄采集（XInput 轮询）。
//
// 采集范围与限制：
//   · 覆盖 Xbox 360 / One / Series 手柄，以及第三方手柄的 **XInput 模式**（最多 4 个）；
//     走 DirectInput 模式的设备不在覆盖范围内（可在系统里切到 XInput 模式）。
//   · 数字量按键：ABXY、肩键 LB/RB、十字键、摇杆按下 L3/R3、View/Menu —— 按**上升沿**计数。
//   · 模拟量：摇杆位移与扳机行程按**里程表模型**累计总行程（不保存轨迹）。
//       - 摇杆是二维量，用**欧氏距离**逐步累加（绕圈推杆也能正确计程）
//       - 扳机是一维量，用相邻采样的差值绝对值累加
//       - 静止抖动由噪声门限滤除；手柄断连重连时重置增量基准（避免假跳变）
//       - 采样间隔由偏好档位决定（低 33ms / 中 16ms / 高 8ms，见 padpref.h）
//     行程增量提交给 storage 按日聚合（PadTravelAdd）。
//   · Xbox 键（手柄中间的 Guide 键）由系统独占上报，XInput 不提供给应用——无法记录。
//   · 手柄不产生键盘/鼠标消息，故与 WH_KEYBOARD_LL / WH_MOUSE_LL 无关，单独轮询。
//
// 由 InstallHook()/RemoveHook() 统一启停，生命期与键鼠钩子一致。

#include <windows.h>

// XInput 的 XINPUT_GAMEPAD / XINPUT_STATE 二进制布局。
// 自行声明，避免依赖 SDK 头文件的版本差异（只用到这两个 POD）。
struct XiGamepad {
    unsigned short wButtons;
    unsigned char bLeftTrigger;
    unsigned char bRightTrigger;
    short sThumbLX;
    short sThumbLY;
    short sThumbRX;
    short sThumbRY;
};
struct XiState {
    unsigned long dwPacketNumber;
    XiGamepad Gamepad;
};

// 返回 ERROR_SUCCESS(0) 表示该槽位有手柄
using XiGetStateFn = unsigned long(WINAPI*)(unsigned long, XiState*);

// 跑一轮轮询；返回有手柄的槽位数。
// 把 XInputGetState 做成参数，是为了让单测能注入假的手柄数据——
// "摇杆值到底有没有被发出去"这件事，没有真手柄在 CI 上否则根本测不了。
int PollOnceWith(XiGetStateFn getState);

// 一轮采集里挑出"第一个已连接手柄"的手柄数据，**按值**放进 out。
//
// 为什么要单独抽成纯函数（而且必须按值）：原来这里是保存 `&st.Gamepad`——
// 指向轮询循环体内、每轮都会重新构造的局部变量 XInputState st。
// 等循环结束再去读那个指针，它早被后面几轮的 `st = {}` 清零了，
// 于是摇杆/扳机**恒为 0**：面板内圈与行程柱永远不动。
// 偏偏按键计数与行程累计都是在循环内当场算完的，所以按键有反应、分数照涨，
// 现象极具误导性。抽成纯函数后可以直接喂数组单测，把这条钉死。
bool PickFirstConnected(int slotCount, const bool ok[], const XiGamepad pads[], XiGamepad* out);

// 启动手柄轮询：重复调用无副作用；缺少 XInput 运行库时静默降级（手柄不计数，键鼠照常）
void GamepadStart();

// 停止手柄轮询并清掉手柄的实时按下状态（避免退出后键面卡在按下态）
void GamepadStop();
