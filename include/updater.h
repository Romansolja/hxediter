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
    uint64_t      bytes_received = 0;
    uint64_t      bytes_total    = 0;
    std::string   installer_path;
};

/* Reads the debounce state file and spawns a check thread if the last
 * check was more than kDebounceSeconds ago. Called once at startup. */
void InitAndMaybeCheck();

/* Manual trigger for the "Check now" button. Spawns a detached thread
 * that ignores debounce. Safe to call even if a check is already running
 * (the second call is a no-op via the in-progress guard). */
void StartCheck();

/* Spawned by the "Install and restart" button. Downloads the installer
 * + SHA256SUMS, verifies, writes installer_path on success. */
void StartDownload();

/* Thread-safe snapshot of current state. Returns by value. */
Snapshot GetSnapshot();

/* Called during shutdown so in-flight worker threads drop their results
 * instead of writing back into module-scope state. */
void RequestAbandon();

/* For use by main.cpp if ShellExecute of the helper fails — surfaces
 * the error back into the Snapshot so the Settings popup can show it. */
void SetLaunchError(std::string msg);

} /* namespace updater */
