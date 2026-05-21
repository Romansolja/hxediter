#pragma once

#include <string>

namespace platform {

/* Returns a path prefix (with trailing slash) that the font loader and
 * other read-only-asset paths should prepend to their relative "assets/..."
 * strings.
 *
 * Resolves to "[[NSBundle mainBundle] resourcePath]/" — points inside
 * the .app at runtime, so a bundle launched from /Applications can
 * still find its fonts. Cached in a static local on first call. */
const std::string& ResourceDir();

/* Returns a writable per-user directory (with trailing slash) for app
 * state like imgui.ini. Created if it doesn't exist.
 *
 * Resolves to "~/Library/Application Support/hxediter/". Without this,
 * ImGui's default cwd-relative "imgui.ini" silently fails to save when
 * the app is launched from /Applications (cwd = /). */
const std::string& AppSupportDir();

} /* namespace platform */
