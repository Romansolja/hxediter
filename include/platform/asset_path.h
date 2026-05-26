#pragma once

#include <string>

namespace platform {

// Read-only asset prefix (with trailing slash). Resolves to the .app's resourcePath at runtime.
const std::string& ResourceDir();

// Writable per-user dir (with trailing slash). Created if missing.
// macOS: "~/Library/Application Support/hxediter/" — overrides ImGui's cwd-relative default,
// which silently fails to save when launched from /Applications (cwd = /).
const std::string& AppSupportDir();

}
