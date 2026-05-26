#include "path_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <system_error>

DirectoryWalkResult ExpandDirectoryInto(const std::filesystem::path& root,
                                        std::vector<std::string>& out) {
    DirectoryWalkResult result;

    // Hard ceilings — walk runs on the UI thread; ~/Library or a deep network mount would freeze it.
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
        // Sample clock every 1024 entries: keeps deadline tight without per-entry overhead.
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
            // Resume at parent — one bad subtree shouldn't kill the listing. If pop fails, stop.
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
