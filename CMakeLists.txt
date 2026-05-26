cmake_minimum_required(VERSION 3.14)

# SDK selection MUST come before project() — project()'s feature-detection probes bake in
# whatever SDK is active at the time. Later overrides only affect subsequent compile commands.
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

# GLFW
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

# Dear ImGui — no upstream CMake. Target name hx_imgui (not imgui) so a future upstream
# CMakeLists defining `imgui` wouldn't collide via FetchContent_MakeAvailable.
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

find_package(OpenGL REQUIRED)

set(HXEDITER_SOURCES
    src/main.cpp
    src/gui.cpp
    src/hex_editor_core.cpp
    src/undo.cpp
    src/path_utils.cpp
    src/fileops.cpp
    src/ui/theme.cpp
    src/ui/gui_state.cpp
    src/ui/actions.cpp
    src/ui/toolbar.cpp
    src/ui/hex_grid.cpp
    src/ui/status_bar.cpp
    src/ui/start_screen.cpp
    src/ui/help_panel.cpp
    src/ui/settings_panel.cpp
    src/ui/preferences.cpp
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
    hx_imgui glfw OpenGL::GL
    "-framework Cocoa"
)

target_compile_definitions(hxediter PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Bake source dir for platform::ResourceDir()'s out-of-bundle fallback (debugger, CI).
target_compile_definitions(hxediter PRIVATE
    HXEDITER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# OpenGL is deprecated on macOS 10.14+ but still works — silence until we switch to Metal.
if(APPLE)
    target_compile_definitions(hxediter PRIVATE GL_SILENCE_DEPRECATION)
endif()

# MACOSX_BUNDLE_ICON_FILE intentionally unset — a non-existent icon name triggers Console
# warnings and codesign advisories. To add an icon: drop hxediter.icns in assets/ and
# re-add MACOSX_BUNDLE_ICON_FILE "hxediter.icns" below.
set_target_properties(hxediter PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "HxEditer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.romansolja.hxediter"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 Romansolja. MIT licensed.")

# Stage fonts into Contents/Resources/assets every build — platform::ResourceDir() reads
# from the bundle, so font updates need to land there without a reconfigure.
add_custom_command(TARGET hxediter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_BUNDLE_CONTENT_DIR:hxediter>/Resources/assets
    COMMENT "Staging assets into hxediter.app/Contents/Resources"
)

# MACOSX_BUNDLE needs BUNDLE DESTINATION — RUNTIME would copy only the Mach-O and orphan Resources.
install(TARGETS hxediter BUNDLE DESTINATION .)

# CPack — DragNDrop DMG
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
