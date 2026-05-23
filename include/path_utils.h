#pragma once

#include <filesystem>
#include <string>
#include <vector>

// All path strings flowing through the editor (argv, GLFW drop callback,
// NSOpenPanel) are UTF-8. These helpers wrap the std::filesystem
// path<->UTF-8 conversion in one place so the C++17 u8string/u8path
// distinction doesn't leak into every caller.

inline std::filesystem::path PathFromUtf8(const std::string& s) {
    return std::filesystem::u8path(s);
}

inline std::string PathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Walks `root` recursively, appending regular files (UTF-8) to `out` in
// alphabetical order. Permission errors skipped via skip_permission_denied;
// all errors absorbed via std::error_code, so this is safe from C callback
// hooks (e.g. GLFW drop) where a thrown exception would propagate through
// the event loop and abort the process.
void ExpandDirectoryInto(const std::filesystem::path& root,
                         std::vector<std::string>& out);

// Returns the last path component of a UTF-8 path. Trailing slashes are
// trimmed first, so "/foo/bar/" yields "bar". Returns "" for inputs that
// are empty or consist only of slashes — callers pick their own fallback
// label (e.g. "(unnamed)" or the original path).
inline std::string PathBasename(const std::string& utf8_path) {
    std::string s = utf8_path;
    while (!s.empty() && s.back() == '/') s.pop_back();
    size_t slash = s.find_last_of('/');
    if (slash == std::string::npos) return s;
    return s.substr(slash + 1);
}
