cmake_minimum_required(VERSION 3.14)
project(hxediter
    VERSION 4.1.0
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
# nlohmann/json — single-header JSON parser for the GitHub releases API
# ---------------------------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# OpenGL (system library)
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# hxediter executable
# ---------------------------------------------------------------------------
set(HXEDITER_SOURCES
    src/main.cpp
    src/gui.cpp
    src/hex_editor_core.cpp
    src/fileops.cpp
    src/undo.cpp
    src/updater.cpp
    src/ui/theme.cpp
    src/ui/gui_state.cpp
    src/ui/actions.cpp
    src/ui/toolbar.cpp
    src/ui/hex_grid.cpp
    src/ui/status_bar.cpp
    src/ui/start_screen.cpp
    src/ui/help_panel.cpp
    src/ui/settings_panel.cpp
    ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

if(WIN32)
    list(APPEND HXEDITER_SOURCES src/win_drop_target.cpp)
endif()

add_executable(hxediter ${HXEDITER_SOURCES})

target_include_directories(hxediter PRIVATE
    include
    ${imguifiledialog_SOURCE_DIR}
)
target_link_libraries(hxediter PRIVATE
    imgui glfw OpenGL::GL
    nlohmann_json::nlohmann_json
    winhttp bcrypt ole32 shell32
)

target_compile_definitions(hxediter PRIVATE APP_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Static-link the MinGW C++ runtime so the binary is self-contained.
# Without this, the exe depends on libstdc++-6.dll, libgcc_s_seh-1.dll,
# and libwinpthread-1.dll which are only present in the MinGW toolchain.
# ---------------------------------------------------------------------------
if(MINGW)
    target_link_options(hxediter PRIVATE -static -mwindows)
endif()

# ---------------------------------------------------------------------------
# updater-helper — tiny companion exe that waits for hxediter to exit
# before ShellExecute-ing the NSIS installer with UAC elevation. The main
# app copies the installed helper to %TEMP% at update time so NSIS's
# uninstall-before-install step can delete the installed copy freely.
# ---------------------------------------------------------------------------
add_executable(hxediter-updater-helper WIN32 tools/updater_helper/main.cpp)
if(MINGW)
    target_link_options(hxediter-updater-helper PRIVATE -static -mwindows)
endif()

# Stage runtime assets (fonts) next to the binary so the relative path
# "assets/fonts/..." used by the font loader resolves regardless of the
# working directory CLion's Run Configuration chooses.
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
install(TARGETS hxediter hxediter-updater-helper RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
install(FILES LICENSE.txt DESTINATION .)

# ---------------------------------------------------------------------------
# CPack / NSIS installer
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "HxEditer")
set(CPACK_PACKAGE_VENDOR "Romansolja")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A hex editor built with Dear ImGui")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "HxEditer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_DISPLAY_NAME "HxEditer")
set(CPACK_NSIS_PACKAGE_NAME "HxEditer")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_HELP_LINK "https://github.com/Romansolja/hxediter")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/Romansolja/hxediter")
set(CPACK_NSIS_INSTALLED_ICON_NAME "hxediter.exe")

# Start Menu shortcut
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\HxEditer.lnk' '$INSTDIR\\\\hxediter.exe'"
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\HxEditer.lnk'"
)

# Desktop shortcut
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
    "CreateShortCut '$DESKTOP\\\\HxEditer.lnk' '$INSTDIR\\\\hxediter.exe'"
)
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
    "Delete '$DESKTOP\\\\HxEditer.lnk'"
)

include(CPack)

add_custom_target(make_installer
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running CPack / NSIS to produce HxEditer-${PROJECT_VERSION}-win64.exe"
)