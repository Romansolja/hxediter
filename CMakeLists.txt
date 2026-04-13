cmake_minimum_required(VERSION 3.10)
project(hxediter C)

set(CMAKE_C_STANDARD 11)

add_executable(hxediter
    src/main.c
    src/display.c
    src/fileops.c
    src/undo.c
)

target_include_directories(hxediter PRIVATE include)
