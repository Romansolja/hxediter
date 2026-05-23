#pragma once

#include <string>

namespace platform {

// Prefix (with trailing slash) for read-only asset paths.
// Resolves to "[[NSBundle mainBundle] resourcePath]/" — points inside
// the .app at runtime, so a bundle launched from /Applications can still
// find its fonts. Cached in a static local on first call.
const std::string& ResourceDir();

// Writable per-user directory (with trailing slash) for app state like
// imgui.ini. Created if it doesn't exist.
// Resolves to "~/Library/Application Support/hxediter/". Without this,
// ImGui's default cwd-relative "imgui.ini" silently fails to save when
// the app is launched from /Applications (cwd = /).
const std::string& AppSupportDir();

} // namespace platform
