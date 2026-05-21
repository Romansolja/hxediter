#pragma once

#include <string>

namespace platform {

/* Returns a path prefix (with trailing slash) that the font loader and
 * other read-only-asset paths should prepend to their relative "assets/..."
 * strings.
 *
 * macOS: "[[NSBundle mainBundle] resourcePath]/" — points inside the .app
 *        at runtime, so a bundle launched from /Applications can still
 *        find its fonts. Cached in a static local on first call.
 * Windows / Linux: empty string. The build's POST_BUILD step copies
 *        ./assets next to the binary, so the existing relative paths
 *        resolve against the cwd-or-binary-dir as before. */
const std::string& ResourceDir();

/* Returns a writable per-user directory (with trailing slash) for app
 * state like imgui.ini. Created if it doesn't exist.
 *
 * macOS: "~/Library/Application Support/hxediter/". Without this,
 *        ImGui's default cwd-relative "imgui.ini" silently fails to
 *        save when the app is launched from /Applications (cwd = /).
 * Windows / Linux: empty string. Caller leaves io.IniFilename at its
 *        default ("imgui.ini") so existing behavior is preserved. */
const std::string& AppSupportDir();

} /* namespace platform */
