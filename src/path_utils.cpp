#include "path_utils.h"

#include <algorithm>
#include <system_error>

namespace {

/* POSIX symlinks aren't followed because we never opt in to
 * directory_options::follow_directory_symlink — the std::filesystem
 * iterator already skips them. No reparse-point analogue on macOS,
 * so this is a deliberate no-op kept as a hook for future per-entry
 * filtering. */
bool IsReparsePoint(const std::filesystem::path& p) {
    (void)p;
    return false;
}

}  /* namespace */

bool PlatformPathStartsWith(const std::filesystem::path& candidate,
                            const std::filesystem::path& prefix) {
    auto cit = candidate.begin();
    auto pit = prefix.begin();
    for (; pit != prefix.end(); ++cit, ++pit) {
        if (cit == candidate.end()) return false;
        if (!PlatformBasenameEquals(PathToUtf8(*cit), PathToUtf8(*pit))) {
            return false;
        }
    }
    return true;
}

std::filesystem::path PlatformPathRelative(const std::filesystem::path& abs,
                                           const std::filesystem::path& root) {
    if (!PlatformPathStartsWith(abs, root)) return abs;

    /* Re-walk the prefix to find where the suffix starts.
     * std::filesystem::path::lexically_relative would do this in one
     * call, but iterating mirrors what PlatformPathStartsWith already
     * did, so the result is internally consistent. O(N) and irrelevant
     * for paths. */
    auto ait = abs.begin();
    auto rit = root.begin();
    for (; rit != root.end(); ++ait, ++rit) {}
    std::filesystem::path out;
    for (; ait != abs.end(); ++ait) {
        out /= *ait;
    }
    return out;
}

void ExpandDirectoryInto(const std::filesystem::path& root,
                         std::vector<std::string>& out,
                         const std::function<bool(const std::filesystem::path&)>& skip_dir) {
    /* IsReparsePoint is a no-op on POSIX; left as a hook for future
     * filtering. */
    if (IsReparsePoint(root)) return;

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) return;

    std::vector<std::filesystem::path> collected;
    std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        std::error_code step_ec;

        bool is_dir = it->is_directory(step_ec);
        if (!step_ec && is_dir) {
            if (IsReparsePoint(it->path())) {
                it.disable_recursion_pending();
            } else if (skip_dir && skip_dir(it->path())) {
                /* Caller-supplied prune (e.g. triage skips .git, .venv,
                 * node_modules, __pycache__). Same mechanism as the
                 * reparse-point guard — drop the recursion intent
                 * before incrementing the iterator. */
                it.disable_recursion_pending();
            }
        }

        std::error_code reg_ec;
        bool regular = it->is_regular_file(reg_ec);
        if (!reg_ec && regular) collected.push_back(it->path());
        it.increment(step_ec);
        if (step_ec) break;
    }
    std::sort(collected.begin(), collected.end());
    for (const auto& p : collected) out.push_back(PathToUtf8(p));
}

void ExpandDirectoryInto(const std::filesystem::path& root,
                         std::vector<std::string>& out) {
    ExpandDirectoryInto(root, out, {});
}
