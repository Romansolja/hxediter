#pragma once

#include <filesystem>
#include <string>
#include <vector>

/* All path strings that flow through the editor (argv, GLFW drop
 * callback, NSOpenPanel results) are UTF-8. These helpers wrap the
 * std::filesystem path<->UTF-8 conversion in one place so the C++17
 * `u8string` / `u8path` distinction (and any future C++20 churn around
 * std::u8string) doesn't leak into every caller. */

inline std::filesystem::path PathFromUtf8(const std::string& s) {
    return std::filesystem::u8path(s);
}

inline std::string PathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

/* Walk `root` recursively, collect regular files, append their UTF-8 paths
 * to `out` in alphabetical order. Permission errors are skipped silently
 * (`directory_options::skip_permission_denied`). All errors are absorbed
 * via std::error_code so this is safe to call from C callback hooks like
 * GLFW's drop callback, where a thrown exception would propagate up
 * through the event loop and abort the process. */
void ExpandDirectoryInto(const std::filesystem::path& root,
                         std::vector<std::string>& out);
