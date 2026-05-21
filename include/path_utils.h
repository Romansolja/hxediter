#pragma once

#include <filesystem>
#include <functional>
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

/* Same idea as PathToUtf8 but uses generic_u8string() so the result
 * carries forward slashes regardless of platform. Use this for paths
 * that flow into JSON output, audit logs, or anywhere the output is
 * read by humans / other tools across platforms.
 *
 * The reinterpret_cast is a no-op in C++17 (where generic_u8string()
 * returns std::string) and the documented-portable conversion in C++20
 * (where it returns std::u8string). Same pattern as PathToUtf8 above —
 * keeps the C++17/20 difference out of every caller. */
inline std::string PathToGenericUtf8(const std::filesystem::path& p) {
    auto u8 = p.generic_u8string();
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

/* Same as the two-arg overload, but `skip_dir` is consulted for every
 * subdirectory encountered mid-walk. Returning true prunes the entire
 * subtree (the directory's contents are not added and not recursed
 * into). The predicate is NOT applied to `root` itself. Used by the
 * triage scanner to skip regenerable folders (.git, .venv,
 * node_modules, __pycache__) at any depth without blowing the walk
 * budget on their contents. An empty / null target falls back to the
 * unfiltered behaviour. */
void ExpandDirectoryInto(const std::filesystem::path& root,
                         std::vector<std::string>& out,
                         const std::function<bool(const std::filesystem::path&)>& skip_dir);

/* Byte-exact basename equality. POSIX filesystems are case-sensitive,
 * and treating "Thumbs.db" and "thumbs.db" as the same file would
 * surprise a user who deliberately keeps both. (APFS defaults to
 * case-insensitive at the filesystem level, but the kernel still
 * preserves the case the user typed — comparing bytewise here matches
 * the user's mental model of "did I name them the same?".) */
inline bool PlatformBasenameEquals(const std::string& a, const std::string& b) {
    return a == b;
}

/* Path-component containment: true iff `prefix` is an ancestor of (or equal
 * to) `candidate`, comparing component-by-component under the same byte-
 * exact policy as PlatformBasenameEquals. Operates on path components,
 * so a sibling like `<root>_backup` is correctly NOT considered to start
 * with `<root>`. Used as the destination-escape guard in the move
 * executor — getting this right matters because it's a control-boundary
 * check. */
bool PlatformPathStartsWith(const std::filesystem::path& candidate,
                            const std::filesystem::path& prefix);

/* Strip `root` from the front of `abs` and return the remainder, when
 * `abs` is contained in `root` per PlatformPathStartsWith. Falls back to
 * returning `abs` unchanged if the containment check fails. */
std::filesystem::path PlatformPathRelative(const std::filesystem::path& abs,
                                           const std::filesystem::path& root);
