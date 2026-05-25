#include "path_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <system_error>

DirectoryWalkResult ExpandDirectoryInto(const std::filesystem::path& root,
                                        std::vector<std::string>& out) {
    DirectoryWalkResult result;

    // Hard ceilings. The walk runs on the UI thread; without these, dropping
    // ~/Library or a deep network mount would freeze the editor.
    constexpr size_t kMaxFiles = 50000;
    constexpr auto   kMaxWalk  = std::chrono::milliseconds(500);

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        result.ok = false;
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + kMaxWalk;

    std::vector<std::filesystem::path> collected;
    std::filesystem::recursive_directory_iterator end;
    size_t since_clock_check = 0;
    bool   logged_step_error = false;

    while (it != end) {
        if (collected.size() >= kMaxFiles) {
            result.truncated_by_count = true;
            break;
        }
        // clock_gettime is cheap but not free; sampling every 1024 entries
        // keeps the deadline tight (~few ms slack at typical 100k entries/s)
        // without per-entry overhead.
        if ((++since_clock_check & 0x3FF) == 0 &&
            std::chrono::steady_clock::now() > deadline) {
            result.truncated_by_time = true;
            break;
        }

        std::error_code reg_ec;
        bool regular = it->is_regular_file(reg_ec);
        if (!reg_ec && regular) collected.push_back(it->path());

        std::error_code step_ec;
        it.increment(step_ec);
        if (step_ec) {
            ++result.step_errors;
            if (!logged_step_error) {
                std::fprintf(stderr,
                             "ExpandDirectoryInto: skipping subtree after "
                             "increment error: %s\n",
                             step_ec.message().c_str());
                logged_step_error = true;
            }
            // Abandon the current subdirectory and resume at its parent;
            // a single permission glitch or vanished subtree shouldn't kill
            // the rest of the listing. If pop also fails we're out of
            // recovery moves and stop cleanly.
            std::error_code pop_ec;
            it.pop(pop_ec);
            if (pop_ec) break;
        }
    }

    std::sort(collected.begin(), collected.end());
    out.reserve(out.size() + collected.size());
    for (const auto& p : collected) out.push_back(PathToUtf8(p));
    return result;
}
