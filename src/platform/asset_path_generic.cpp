/* Non-Apple impls of platform::ResourceDir() and platform::AppSupportDir().
 * Both return empty strings:
 *   - ResourceDir(): the build's POST_BUILD step copies ./assets next to
 *     the binary, so existing relative "assets/..." font paths resolve
 *     against cwd as before. An empty prefix means no change.
 *   - AppSupportDir(): caller sees empty -> leaves io.IniFilename at the
 *     ImGui default ("imgui.ini"), preserving the existing cwd-relative
 *     state-file behavior on Windows / Linux. */

#ifndef __APPLE__

#include "platform/asset_path.h"

namespace platform {

const std::string& ResourceDir() {
    static const std::string empty;
    return empty;
}

const std::string& AppSupportDir() {
    static const std::string empty;
    return empty;
}

} /* namespace platform */

#endif /* !__APPLE__ */
