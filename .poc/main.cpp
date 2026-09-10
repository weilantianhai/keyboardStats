// EUI-NEO 集成冒烟测试：验证 CMake + MinGW 工具链、中文渲染、托盘配置
#include "eui_neo.h"

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("KeyboardStats PoC")
        .pageId("kbstats_poc")
        .clearColor({0.06f, 0.07f, 0.09f, 1.0f})
        .windowSize(960, 640)
        .tray(true)
        .trayTitle("KeyboardStats");
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.column("root")
        .size(screen.width, screen.height)
        .padding(32.0f)
        .content([&] {
            ui.text("hello")
                .text("键盘热力统计 · PoC 冒烟测试 ABC 123")
                .fontSize(28.0f)
                .build();
        })
        .build();
}

} // namespace app
