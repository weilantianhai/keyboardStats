cmake_minimum_required(VERSION 3.14)
project(KeyboardStats LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/.vendor/eui-neo EUI-NEO)

add_executable(KeyboardStats
    src/app.cpp
    src/theme.cpp
    src/fontscale.cpp
    # 页面按页拆分：共享小构件 + 各页面各一个 TU（原 pages.cpp 单体 2300 行）
    src/pages.cpp            # 头部 / 控制行 / 主页看板 / 热力图页 / 按键计数列表
    src/pages_common.cpp     # 跨页面共享的小构件与状态
    src/pages_hist.cpp       # 直方图页
    src/pages_settings.cpp   # 设置页（管理员/自启动/字体/记录管理）
    src/pages_theme.cpp      # 主题页 + HSL 取色浮层
    src/pages_dialogs.cpp    # 关窗确认 / 首启引导弹窗
    src/heatnorm.cpp         # 热力归一化（纯逻辑，无框架依赖）
    src/recorder.cpp
    src/win/autostart.cpp
    src/win/adminmode.cpp
    src/hook.cpp
    src/storage.cpp
    src/timeutil.cpp
    src/resources/app.rc    # 应用清单：requireAdministrator（游戏等高完整性窗口下也可记录）
)
eui_neo_configure_app(KeyboardStats)

# 记录管理用到 Win32 文件对话框、ShellExecute 与文件夹选择（SHBrowseForFolder/CoTaskMemFree）；
# 记录进程的托盘图标用 GDI+ 从 assets\icon.png 生成
target_link_libraries(KeyboardStats PRIVATE comdlg32 shell32 ole32 gdiplus)

# 部署运行资源（托盘/窗口图标按 exe 同级 assets\ 解析）
add_custom_command(TARGET KeyboardStats POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:KeyboardStats>/assets
    COMMENT "Deploy assets")
