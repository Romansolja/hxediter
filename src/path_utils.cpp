#include "path_utils.h"

#include <algorithm>
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

/* True iff `p` is a NTFS reparse point (junction or symlink) on Windows.
 * fs::is_symlink correctly identifies symlinks on Windows but NOT
 * junctions (FILE_ATTRIBUTE_REPARSE_POINT with IO_REPARSE_TAG_MOUNT_POINT).
 * recursive_directory_iterator follows junctions as ordinary directories,
 * so without this guard a junction at <root>/foo pointing at C:\ causes
 * the walker to traverse the entire system drive.
 *
 * No-op on non-Windows platforms; POSIX symlinks are excluded by default
 * because we never opt in to directory_options::follow_directory_symlink. */
bool IsReparsePoint(const std::filesystem::path& p) {
#ifdef _WIN32
    const DWORD attrs = ::GetFileAttributesW(p.wstring().c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void)p;
    return false;
#endif
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
    /* Containment check honours the platform case policy. */
    if (!PlatformPathStartsWith(abs, root)) return abs;

    /* Re-walk the prefix to find where the suffix starts.
     * std::filesystem::path::lexically_relative would do this in one
     * call, but it compares components via path::operator== which is
     * case-sensitive on Windows — that would diverge from our
     * PlatformPathStartsWith result for a case-mismatched root, leaving
     * the caller with the "..\..\..\Drive\..." escape form. Iterating
     * here is O(N) again and the cost is irrelevant for paths. */
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
    /* Bail if the root itself is a reparse point. Otherwise a triage
     * scan of `<scan>/junction → C:\` would recurse into C:\ — cheaper
     * to refuse than to detect mid-walk and abort. The user can still
     * drop the actual target folder if they meant to scan it. */
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

        /* Skip recursing INTO reparse points (NTFS junctions, Windows
         * symlinks). The directory entry itself is a "directory" from
         * the iterator's view, so it doesn't get added to `collected`
         * (which is regular-files-only); we just need to make sure we
         * don't descend into it. */
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
