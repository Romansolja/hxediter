cmake_minimum_required(VERSION 3.10)
project(hxediter CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hxediter
    src/main.cpp
    src/hex_editor_core.cpp
    src/display.cpp
    src/fileops.cpp
    src/undo.cpp
)

target_include_directories(hxediter PRIVATE include)
