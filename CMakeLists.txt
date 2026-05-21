cmake_minimum_required(VERSION 3.14)

# Apple SDK selection MUST happen before project(): the compiler-id and
# feature-detection probes that project() fires off bake in whatever SDK
# was active at that moment. Setting CMAKE_OSX_DEPLOYMENT_TARGET later
# only affects subsequent compile commands — any compile test between
# project() and the override would silently use the host SDK's default
# (currently 15.x on this M5). enable_language(OBJCXX) is deferred until
# after project() because it itself requires the project to exist.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

project(hxediter
    VERSION 5.1.2
    DESCRIPTION "A hex editor built with Dear ImGui"
    HOMEPAGE_URL "https://github.com/Romansolja/hxediter"
    LANGUAGES CXX
)

if(APPLE)
    enable_language(OBJCXX)
endif()

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
# Dear ImGui — no native CMake support upstream, so we build it ourselves.
# Target is named `hx_imgui` (not `imgui`) to avoid a name collision if a
# future ImGui release ever ships a top-level CMakeLists that defines its
# own `imgui` target — FetchContent_MakeAvailable would then auto-include
# it and our add_library() would fail with a duplicate-target error.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_library(hx_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(hx_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(hx_imgui PUBLIC glfw)

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
# xxHash — single-header hash library (BSD 2-Clause), vendored at
# third_party/xxhash. Exposed as INTERFACE so consumers just include the
# header; the implementation is generated in the one TU that defines
# XXH_IMPLEMENTATION (see src/triage/hasher.cpp).
# ---------------------------------------------------------------------------
add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE third_party/xxhash)

# ---------------------------------------------------------------------------
# hxcore — pure-logic static lib shared by the hxediter GUI and the
# hxsort CLI. No ImGui / GLFW / OpenGL deps. Holds path utilities,
# low-level file I/O, and the triage classification module. Triage source
# files are appended below as each one is implemented; this list is
# always source-of-truth.
# ---------------------------------------------------------------------------
set(HXCORE_SOURCES
    src/path_utils.cpp
    src/fileops.cpp
    src/triage/signatures.cpp
    src/triage/hasher.cpp
    src/triage/classifier.cpp
    src/triage/move_actions.cpp
    src/triage/scanner.cpp
)
add_library(hxcore STATIC ${HXCORE_SOURCES})
target_include_directories(hxcore PUBLIC include)
target_compile_features(hxcore PUBLIC cxx_std_17)
target_link_libraries(hxcore PUBLIC xxhash)

# ---------------------------------------------------------------------------
# hxediter executable
# ---------------------------------------------------------------------------
set(HXEDITER_SOURCES
    src/main.cpp
    src/gui.cpp
    src/hex_editor_core.cpp
    src/undo.cpp
    src/ui/theme.cpp
    src/ui/gui_state.cpp
    src/ui/actions.cpp
    src/ui/toolbar.cpp
    src/ui/hex_grid.cpp
    src/ui/status_bar.cpp
    src/ui/start_screen.cpp
    src/ui/help_panel.cpp
    src/ui/settings_panel.cpp
)

if(APPLE)
    list(APPEND HXEDITER_SOURCES
        src/platform/file_dialogs_macos.mm
        src/platform/macos_bundle.mm)
else()
    message(FATAL_ERROR
        "Build target '${CMAKE_SYSTEM_NAME}' is not supported: the "
        "macos-beta branch is macOS-only. The Windows build lives on "
        "the main-cpp branch.")
endif()

add_executable(hxediter ${HXEDITER_SOURCES})

target_include_directories(hxediter PRIVATE
    include
)
target_link_libraries(hxediter PRIVATE
    hxcore
    hx_imgui glfw OpenGL::GL
    nlohmann_json::nlohmann_json
    "-framework Cocoa"
)

target_compile_definitions(hxediter PRIVATE APP_VERSION="${PROJECT_VERSION}")

# MACOSX_BUNDLE_ICON_FILE deliberately omitted until we ship a real
# hxediter.icns: setting it to a non-existent file makes Console.app
# log a "couldn't load icon" warning every launch and codesign emits
# a (non-fatal) advisory about an unreferenced resource. With the
# property unset, the Info.plist template's @MACOSX_BUNDLE_ICON_FILE@
# substitutes to empty and macOS falls back to a generic icon
# silently. To wire up an icon later: generate hxediter.icns via
# `iconutil -c icns hx.iconset/`, drop it in assets/ (it gets staged
# into Contents/Resources by the POST_BUILD below), and re-add the
# MACOSX_BUNDLE_ICON_FILE "hxediter.icns" line here.
set_target_properties(hxediter PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "HxEditer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.romansolja.hxediter"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 Romansolja. MIT licensed.")

# ---------------------------------------------------------------------------
# hxsort — headless folder-triage CLI. Links hxcore + nlohmann_json only.
# ---------------------------------------------------------------------------
add_executable(hxsort src/cli/hxsort_main.cpp)
target_link_libraries(hxsort PRIVATE
    hxcore
    nlohmann_json::nlohmann_json
)
target_compile_definitions(hxsort PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Stage runtime assets (fonts) into the .app's Contents/Resources/assets
# directory on every build so font updates land without forcing a
# reconfigure. platform::ResourceDir() returns [[NSBundle mainBundle]
# resourcePath] at runtime, so the fonts must live inside the bundle.
add_custom_command(TARGET hxediter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_BUNDLE_CONTENT_DIR:hxediter>/Resources/assets
    COMMENT "Staging assets into hxediter.app/Contents/Resources"
)

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
# hxediter is a MACOSX_BUNDLE target, which must use BUNDLE DESTINATION
# rather than RUNTIME — installing a .app via RUNTIME copies only the
# executable Mach-O and orphans Contents/Resources. The bundle is
# self-contained (assets are inside Contents/Resources via POST_BUILD;
# LICENSE/copyright are surfaced in Info.plist), so we don't ship loose
# asset / hxsort / license files in the DMG — only the .app and the
# /Applications symlink CPack's DragNDrop generator auto-adds. Cleaner
# mount UX: one icon to drag.
install(TARGETS hxediter BUNDLE DESTINATION .)

# ---------------------------------------------------------------------------
# CPack — DragNDrop DMG
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "HxEditer")
set(CPACK_PACKAGE_VENDOR "Romansolja")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A hex editor built with Dear ImGui")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "HxEditer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

set(CPACK_GENERATOR "DragNDrop")
set(CPACK_DMG_VOLUME_NAME "HxEditer")
set(CPACK_PACKAGE_FILE_NAME "HxEditer-${PROJECT_VERSION}-macos-arm64")

include(CPack)

add_custom_target(make_installer
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running CPack to produce the DragNDrop .dmg disk image"
)
