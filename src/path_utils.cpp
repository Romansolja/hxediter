#include "path_utils.h"

#include <algorithm>
#include <system_error>

void ExpandDirectoryInto(const std::filesystem::path& root,
                         std::vector<std::string>& out) {
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
        std::error_code reg_ec;
        bool regular = it->is_regular_file(reg_ec);
        if (!reg_ec && regular) collected.push_back(it->path());
        it.increment(step_ec);
        if (step_ec) break;
    }
    std::sort(collected.begin(), collected.end());
    for (const auto& p : collected) out.push_back(PathToUtf8(p));
}
