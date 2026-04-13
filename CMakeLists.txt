cmake_minimum_required(VERSION 3.10)
project(hxediter C)

set(CMAKE_C_STANDARD 11)

add_executable(hxediter
    main.c
    display.c
    fileops.c
    undo.c
)
