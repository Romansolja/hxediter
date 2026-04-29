#pragma once

/* triage::scanner — async folder scan + classification + duplicate
 * detection. Single global instance per process; the GUI panel and the
 * CLI both consume the same API.
 *
 * Lifecycle:
 *   StartScan()  -> spawns one detached outer thread
 *   GetProgress()-> snapshot copy, safe to call every frame
 *   RequestCancel()-> sets atomic; workers honour between files
 *   WaitForCompletion() -> blocks until state is terminal (CLI uses this)
 *   Reset()      -> clears state when starting fresh
 *
 * Mirrors the WithSnap-and-Snapshot pattern from updater.cpp; same shape,
 * different payload. */

#include "triage/classifier.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace triage {

enum class ScanState : std::uint8_t {
    Idle      = 0,
    Walking   = 1,
    Classifying = 2,
    Hashing   = 3,
    Done      = 4,
    Cancelled = 5,
    Failed    = 6,
};

const char* ScanStateName(ScanState s);

/* One bucket in the duplicate-detection result. `verdict_indices` are
 * indices into ScanProgress::files. The lex-smallest (by UTF-8 path) is
 * always at index 0; the rest are non-canonical duplicates. */
struct DupGroup {
    std::uint64_t hash = 0;
    std::uint64_t size = 0;
    std::vector<std::int32_t> verdict_indices;
};

/* Snapshot of in-progress / completed scan state. Cheap to copy.
 * `files` and `dup_groups` are populated incrementally during the scan
 * and complete after state == Done.
 *
 * `config` is captured at StartScan time so consumers (the GUI panel,
 * any future auditing code) can read the bucket names, thresholds, and
 * feature toggles that drove this scan WITHOUT having to maintain a
 * parallel copy. Critical for the GUI — the panel's Move buttons must
 * route to the same bucket names the scanner used for its top-level
 * skip filter, otherwise the second scan would re-pick-up the moved
 * files. */
struct ScanProgress {
    ScanState     state = ScanState::Idle;
    std::uint64_t files_walked = 0;
    std::uint64_t files_classified = 0;
    std::uint64_t files_hashed = 0;
    std::uint64_t total_to_hash = 0;
    std::string   current_path;        /* throttled UTF-8 hint              */
    std::string   error_message;       /* set when state == Failed          */
    std::filesystem::path root;
    Config                  config;
    std::vector<FileVerdict> files;
    std::vector<DupGroup>    dup_groups;
};

/* Optional per-file callback fired from worker threads as each file is
 * classified (BEFORE the hash phase, so content_hash / dup_group are
 * not yet set). The scanner serialises calls so the consumer doesn't
 * need its own mutex.
 *
 * Used by hxsort to stream JSONL to stdout without buffering the full
 * verdict list in memory. */
using FileVerdictCallback = std::function<void(const FileVerdict&)>;

/* Optional callback fired AFTER the hash phase completes, once per
 * verdict whose dup metadata changed (content_hash, dup_group,
 * dup_canonical, and possibly verdict promoted to Duplicate). Used by
 * hxsort to emit a second pass of "updated" JSONL records, or by the
 * GUI to refresh table rows. */
using FileVerdictUpdateCallback = std::function<void(const FileVerdict&)>;

/* Start a scan. Returns immediately; the scan runs on a detached
 * thread. If a previous scan is still in progress (state == Walking,
 * Classifying, or Hashing), this call is a no-op.
 *
 * Throws ConfigError on invalid cfg subfolder names. */
void StartScan(const std::filesystem::path& root,
               const Config& cfg,
               FileVerdictCallback on_classified = nullptr,
               FileVerdictUpdateCallback on_dup_updated = nullptr);

ScanProgress GetProgress();
void RequestCancel();
void Reset();

/* Block until state is Done/Cancelled/Failed. Used by the CLI; the GUI
 * never calls this (it polls per frame). */
void WaitForCompletion();

}  /* namespace triage */
