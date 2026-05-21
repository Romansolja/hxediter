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
    src/ui/triage_panel.cpp
)

if(WIN32)
    list(APPEND HXEDITER_SOURCES
        src/win_drop_target.cpp
        src/platform/file_dialogs_win32.cpp
        src/platform/asset_path_generic.cpp
        src/updater.cpp)
elseif(APPLE)
    list(APPEND HXEDITER_SOURCES
        src/platform/file_dialogs_macos.mm
        src/platform/macos_bundle.mm
        src/updater_stub.cpp)
else()
    # Non-Apple Unix platforms (Linux, FreeBSD, OpenBSD, Haiku, etc.) are
    # intentionally absent in v1 — no file_dialogs_*.cpp implementation
    # exists for them, but src/ui/start_screen.cpp calls
    # platform::OpenFileDialog / PickFolderDialog unconditionally. Fail
    # loudly at configure time rather than letting the link step die with
    # a wall of undefined-reference noise (the failure mode users would
    # actually hit). When one of these becomes a real target, add a
    # file_dialogs_<platform>.cpp (likely XDG portal / GTK FileChooser /
    # zenity-shell-out on Linux), plus asset_path_generic.cpp +
    # updater_stub.cpp, and replace this with a real branch.
    message(FATAL_ERROR
        "Build target '${CMAKE_SYSTEM_NAME}' is not supported in v1: "
        "no platform::OpenFileDialog implementation exists for non-Apple "
        "Unix platforms. See CMakeLists.txt for the missing pieces.")
endif()

add_executable(hxediter ${HXEDITER_SOURCES})

target_include_directories(hxediter PRIVATE
    include
)
target_link_libraries(hxediter PRIVATE
    hxcore
    hx_imgui glfw OpenGL::GL
    nlohmann_json::nlohmann_json
)
if(WIN32)
    target_link_libraries(hxediter PRIVATE winhttp bcrypt ole32 shell32)
elseif(APPLE)
    target_link_libraries(hxediter PRIVATE "-framework Cocoa")
endif()

target_compile_definitions(hxediter PRIVATE APP_VERSION="${PROJECT_VERSION}")

if(APPLE)
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
endif()

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
if(WIN32)
    add_executable(hxediter-updater-helper WIN32 tools/updater_helper/main.cpp)
    # bcrypt for the SHA256 TOCTOU recheck performed immediately before
    # ShellExecute("runas") — closes the gap between the parent's verify and
    # the elevated launch when %TEMP% is user-writable.
    target_link_libraries(hxediter-updater-helper PRIVATE shell32 bcrypt)
    if(MINGW)
        target_link_options(hxediter-updater-helper PRIVATE -static -mwindows)
    endif()
endif()

# ---------------------------------------------------------------------------
# hxsort — headless folder-triage CLI. Console-mode exe (no -mwindows),
# links hxcore + nlohmann_json only. Same install destination as the
# hex editor so the NSIS package ships both binaries.
# ---------------------------------------------------------------------------
add_executable(hxsort src/cli/hxsort_main.cpp)
target_link_libraries(hxsort PRIVATE
    hxcore
    nlohmann_json::nlohmann_json
)
target_compile_definitions(hxsort PRIVATE APP_VERSION="${PROJECT_VERSION}")
if(MINGW)
    target_link_options(hxsort PRIVATE -static)
endif()

# Stage runtime assets (fonts) next to the binary on every build so font
# updates land without forcing a reconfigure. On macOS the staging target
# is the .app's Contents/Resources/assets directory — platform::ResourceDir()
# returns [[NSBundle mainBundle] resourcePath] at runtime, so the fonts must
# live inside the bundle. On other platforms the relative "assets/fonts/..."
# path used by the font loader resolves against the build directory.
if(APPLE)
    add_custom_command(TARGET hxediter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_BUNDLE_CONTENT_DIR:hxediter>/Resources/assets
        COMMENT "Staging assets into hxediter.app/Contents/Resources"
    )
else()
    add_custom_command(TARGET hxediter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
        COMMENT "Staging assets next to hxediter"
    )
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
if(WIN32)
    install(TARGETS hxediter hxediter-updater-helper hxsort RUNTIME DESTINATION .)
    install(DIRECTORY assets DESTINATION .)
    install(FILES LICENSE.txt DESTINATION .)
elseif(APPLE)
    # hxediter is a MACOSX_BUNDLE target, which must use BUNDLE DESTINATION
    # rather than RUNTIME — installing a .app via RUNTIME copies only the
    # executable Mach-O and orphans Contents/Resources. The bundle is
    # self-contained (assets are inside Contents/Resources via POST_BUILD;
    # LICENSE/copyright are surfaced in Info.plist), so we don't ship loose
    # asset / hxsort / license files in the DMG — only the .app and the
    # /Applications symlink CPack's DragNDrop generator auto-adds. Cleaner
    # mount UX: one icon to drag.
    install(TARGETS hxediter BUNDLE DESTINATION .)
else()
    install(TARGETS hxediter hxsort RUNTIME DESTINATION .)
    install(DIRECTORY assets DESTINATION .)
    install(FILES LICENSE.txt DESTINATION .)
endif()

# ---------------------------------------------------------------------------
# CPack — NSIS installer on Windows, DragNDrop DMG on macOS
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "HxEditer")
set(CPACK_PACKAGE_VENDOR "Romansolja")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A hex editor built with Dear ImGui")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "HxEditer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

if(WIN32)
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
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "HxEditer")
    set(CPACK_PACKAGE_FILE_NAME "HxEditer-${PROJECT_VERSION}-macos-arm64")
endif()

include(CPack)

add_custom_target(make_installer
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running CPack to produce the platform installer / disk image (NSIS .exe on Windows, DragNDrop .dmg on macOS)"
)
