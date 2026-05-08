#pragma once

#include <cstdint>
#include <string>

namespace updater {

enum class CheckState {
    Idle,
    InProgress,
    UpToDate,
    UpdateAvailable,
    NetworkError,
    ParseError,
};

enum class DownloadState {
    Idle,
    InProgress,
    Complete,
    Failed,
};

struct Snapshot {
    CheckState    check         = CheckState::Idle;
    DownloadState download      = DownloadState::Idle;
    std::string   latest_version;
    std::string   error_message;
    std::string   launch_error;
    uint64_t      bytes_received = 0;
    uint64_t      bytes_total    = 0;
    std::string   installer_path;
    /* Lowercase hex SHA256 of installer_path's contents, captured after
     * the integrity check passed in DoDownload. The helper re-verifies
     * against this expected hash before ShellExecute("runas") to close
     * the TOCTOU window between the unprivileged main process's check
     * and the privileged elevation: %TEMP% is user-writable, so any
     * other process running as the user could swap the file in between. */
    std::string   installer_sha256;
};

/* One-shot at startup: spawns a check thread if the last check is older
 * than kDebounceSeconds. */
void InitAndMaybeCheck();

/* Manual "Check now" — ignores debounce; no-op if a check is in flight. */
void StartCheck();

/* Downloads the installer + SHA256SUMS, verifies, writes installer_path. */
void StartDownload();

Snapshot GetSnapshot();

/* Shutdown signal so in-flight workers drop their results rather than
 * writing into module state that's being destroyed. */
void RequestAbandon();

/* Surfaces a ShellExecute failure from main.cpp back into the Snapshot. */
void SetLaunchError(std::string msg);

/* Hands the verified installer path + SHA256 to the caller exactly
 * once. Decouples the handoff from popup visibility so a focus-dismissed
 * Settings popup doesn't strand the download. The hash is forwarded to
 * the elevated helper so it can re-verify before launch. */
bool ConsumeInstallerPath(std::string& out_path, std::string& out_sha256);

/* Reads and clears updater-helper failure log from the previous run. */
bool ConsumeLastLaunchFailure(std::string& out_message);

} /* namespace updater */
