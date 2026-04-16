cmake_minimum_required(VERSION 3.14)
project(hxediter
    VERSION 0.1.0
    DESCRIPTION "A hex editor built with Dear ImGui"
    HOMEPAGE_URL "https://github.com/Romansolja/hxediter"
    LANGUAGES CXX
)

set(PROJECT_LICENSE "MIT")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# GLFW — windowing and input
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ---------------------------------------------------------------------------
# Dear ImGui — no native CMake support, so we build it as a static library
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------
# ImGuiFileDialog — in-ImGui file browser (matches our dark theme)
# SOURCE_SUBDIR points at a non-existent directory so MakeAvailable fetches
# the source without running the upstream CMakeLists.txt. We compile the
# single .cpp directly as part of the hxediter executable below.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imguifiledialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG        v0.6.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-exist
)
FetchContent_MakeAvailable(imguifiledialog)

# ---------------------------------------------------------------------------
# OpenGL (system library)
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# hxediter executable
# ---------------------------------------------------------------------------
add_executable(hxediter
    src/main.cpp
    src/gui.cpp
    src/hex_editor_core.cpp
    src/fileops.cpp
    src/undo.cpp
    src/ui/theme.cpp
    src/ui/gui_state.cpp
    src/ui/actions.cpp
    src/ui/toolbar.cpp
    src/ui/hex_grid.cpp
    src/ui/status_bar.cpp
    src/ui/start_screen.cpp
    src/ui/help_panel.cpp
    ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

target_include_directories(hxediter PRIVATE
    include
    ${imguifiledialog_SOURCE_DIR}
)
target_link_libraries(hxediter PRIVATE imgui glfw OpenGL::GL)

# Stage runtime assets (fonts) next to the binary so the relative path
# "assets/fonts/..." used by the font loader resolves regardless of the
# working directory CLion's Run Configuration chooses.
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
install(TARGETS hxediter RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/hxediter/assets)
install(FILES LICENSE.txt DESTINATION share/hxediter)
