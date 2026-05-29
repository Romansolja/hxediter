# Pure-helper tests link only the TU that defines the helper (no ImGui, no GLFW).
# Integration tests for HexEditorCore link its real dependencies (fileops, undo).
# Each test binary owns its own main() and is registered with add_test() so
# `ctest --output-on-failure` surfaces which group regressed.

add_executable(test_hex_parse
    test_hex_parse.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/hex_parse.cpp
)
target_include_directories(test_hex_parse PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME hex_parse COMMAND test_hex_parse)

add_executable(test_hex_grid_math
    test_hex_grid_math.cpp
)
target_include_directories(test_hex_grid_math PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME hex_grid_math COMMAND test_hex_grid_math)

add_executable(test_hex_editor_core
    test_hex_editor_core.cpp
    ${CMAKE_SOURCE_DIR}/src/hex_editor_core.cpp
    ${CMAKE_SOURCE_DIR}/src/fileops.cpp
    ${CMAKE_SOURCE_DIR}/src/undo.cpp
)
target_include_directories(test_hex_editor_core PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME hex_editor_core COMMAND test_hex_editor_core)

# Bound every test's wall-clock so a regression that reintroduces a blocking
# open (e.g. a FIFO slipping past classification into fopen("rb")) fails the
# run instead of hanging CI indefinitely.
set_tests_properties(hex_parse hex_grid_math hex_editor_core
                     PROPERTIES TIMEOUT 30)
