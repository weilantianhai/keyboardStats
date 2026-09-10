cmake_minimum_required(VERSION 3.14)
project(kbstats_poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.vendor/eui-neo EUI-NEO)

add_executable(kbstats_poc main.cpp)
eui_neo_configure_app(kbstats_poc)
