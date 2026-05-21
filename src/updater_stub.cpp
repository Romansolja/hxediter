/* No-op updater for non-Windows builds. The full updater (src/updater.cpp)
 * is WinHTTP/BCrypt-bound and only compiles on Windows; the macOS / Linux
 * v1 releases ship without auto-update. main.cpp has two unconditional
 * updater:: calls (InitAndMaybeCheck at startup, RequestAbandon at
 * shutdown); the rest of the surface is referenced from the Updates
 * accordion in the Settings popup, which is gated behind #ifdef _WIN32
 * at its call site. The stubs below keep the link table satisfied for
 * any caller that's left untreated. */

#include "updater.h"

namespace updater {

void InitAndMaybeCheck() {}

void StartCheck() {}

void StartDownload() {}

Snapshot GetSnapshot() { return Snapshot{}; }

void RequestAbandon() {}

void SetLaunchError(std::string /*msg*/) {}

bool ConsumeInstallerPath(std::string& /*out_path*/,
                          std::string& /*out_sha256*/) {
    return false;
}

bool ConsumeLastLaunchFailure(std::string& /*out_message*/) {
    return false;
}

} /* namespace updater */
