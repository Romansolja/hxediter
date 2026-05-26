#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// All editor path strings are UTF-8 — these wrap the C++17 u8string/u8path conversion.

inline std::filesystem::path PathFromUtf8(const std::string& s) {
    return std::filesystem::u8path(s);
}

inline std::string PathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

struct DirectoryWalkResult {
    bool   ok                 = true;
    bool   truncated_by_count = false;
    bool   truncated_by_time  = false;
    size_t step_errors        = 0;
};

// Recursive walk of `root`, appending UTF-8 file paths to `out` alphabetically.
// Noexcept (errors absorbed via std::error_code) — safe inside C callback hooks like GLFW drop.
// Bounded by file-count and wall-time caps to keep the UI thread responsive.
DirectoryWalkResult ExpandDirectoryInto(const std::filesystem::path& root,
                                        std::vector<std::string>& out);

// Last path component, trailing slashes trimmed first. Returns "" for empty / slashes-only.
inline std::string PathBasename(const std::string& utf8_path) {
    std::string s = utf8_path;
    while (!s.empty() && s.back() == '/') s.pop_back();
    size_t slash = s.find_last_of('/');
    if (slash == std::string::npos) return s;
    return s.substr(slash + 1);
}
