cmake_minimum_required(VERSION 3.14)
project(KeyboardStats LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/.vendor/eui-neo EUI-NEO)

add_executable(KeyboardStats
    src/app.cpp
    src/theme.cpp
    src/fontscale.cpp
    src/pages.cpp
    src/win/autostart.cpp
    src/hook.cpp
    src/storage.cpp
    src/timeutil.cpp
)
eui_neo_configure_app(KeyboardStats)

# 记录管理用到 Win32 文件对话框与 ShellExecute
target_link_libraries(KeyboardStats PRIVATE comdlg32 shell32)

# 部署运行资源（托盘/窗口图标按 exe 同级 assets\ 解析）
add_custom_command(TARGET KeyboardStats POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:KeyboardStats>/assets
    COMMENT "Deploy assets")
