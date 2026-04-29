#pragma once

/* triage::move_actions — planning and execution of file moves into
 * triage subfolders (_junk/_review/_duplicates).
 *
 * Two planning entry points share the same execute path:
 *   PlanAutoMoves   — verdict drives destination bucket; used by `hxsort --move`.
 *   PlanBucketMoves — caller picks the bucket; used by GUI's three buttons.
 *
 * The execute step does the actual filesystem rename with collision-safe
 * suffixes, cross-device-rename fallback, path-escape guard, and an
 * optional audit log. */

#include "triage/classifier.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace triage {

/* One concrete move from src to dst. dst is the *intended* destination;
 * if it exists at execute time, ExecuteMoves applies a " (N)" suffix
 * before performing the rename. `bucket_reason` is the Verdict that
 * drove this file into its target bucket — used for the audit log
 * column and for tooling that wants to filter by why-was-this-moved. */
struct MoveOp {
    std::filesystem::path src;
    std::filesystem::path dst;
    Verdict               bucket_reason = Verdict::Junk;
};

/* Per-op outcome. `final_dst` is what the file actually ended up at
 * (may differ from op.dst if a collision suffix was applied). Empty
 * for non-Moved statuses. */
enum class MoveStatus : std::uint8_t {
    Moved    = 0,
    Failed   = 1,  /* OS-level error (permission, vanished, etc.)  */
    Rejected = 2,  /* path-escape guard, collision suffix exhausted */
};

struct MoveResult {
    MoveOp                op;
    MoveStatus            status = MoveStatus::Failed;
    std::filesystem::path final_dst;       /* set when status == Moved */
    std::string           error_message;   /* populated for Failed/Rejected */
};

/* Build a move plan with verdict-driven bucket routing.
 *   Verdict::Junk      -> cfg.junk_subfolder
 *   Verdict::Empty     -> cfg.junk_subfolder       (Empty is treated as junk
 *                                                   for routing — user usually
 *                                                   wants them gone, and _junk/
 *                                                   is a reasonable inbox)
 *   Verdict::Duplicate -> cfg.duplicates_subfolder
 *   Verdict::Unknown   -> cfg.review_subfolder
 *   Verdict::Useful    -> not moved
 *   Verdict::Error     -> not moved (logged separately by caller)
 *
 * Destination paths preserve the source's relative position under `root`.
 * For a file at <root>/sub/Thumbs.db with bucket "_junk", dst is
 * <root>/_junk/sub/Thumbs.db. No flattening; original directory shape is
 * preserved inside the bucket so reversibility is "drag the contents of
 * _junk back where they came from."
 *
 * Throws ConfigError if cfg subfolder names are invalid. */
std::vector<MoveOp> PlanAutoMoves(
    const std::vector<FileVerdict>& verdicts,
    const Config& cfg,
    const std::filesystem::path& root);

/* Build a move plan where every selected file goes to one bucket.
 * `bucket_name` must be a valid subfolder name (IsValidSubfolderName());
 * `selected_indices` are indices into `verdicts` (out-of-range is silently
 * skipped). */
std::vector<MoveOp> PlanBucketMoves(
    const std::vector<FileVerdict>& verdicts,
    const std::vector<std::size_t>& selected_indices,
    const std::string& bucket_name,
    const std::filesystem::path& root);

struct ExecuteOptions {
    /* Whether to write an audit log alongside the moved files. The log
     * destination is NOT user-configurable: it always lands at
     * <root>/<cfg.junk_subfolder>/triage-log-<ts>.txt, regardless of
     * which bucket(s) this batch touched. Rationale: one canonical
     * location keeps the log discoverable across runs, and `_junk` is
     * where users go first when they want to undo a triage. */
    bool write_audit_log = true;

    /* Optional cancellation flag. ExecuteMoves polls this between
     * filesystem operations and returns early when set. The Back
     * button in the triage panel signals this so the user isn't
     * frozen waiting for a 50k-file move to finish before they can
     * leave the panel. */
    std::atomic<bool>* cancel_flag = nullptr;
};

/* Execute moves against the live filesystem. Per-op:
 *   1. Path-escape guard: weakly_canonical(dst) must remain under
 *      weakly_canonical(root). Rejected on violation.
 *   2. Lazy-create destination parent directory.
 *   3. Collision-safe suffix: if dst exists, try "name (1).ext",
 *      "name (2).ext", ..., up to (kMaxCollisionSuffix). Beyond that,
 *      Reject and log.
 *   4. std::filesystem::rename; on errc::cross_device_link, fall back
 *      to copy_file + remove.
 * Per-op failures are reported in MoveResult; the batch never aborts.
 * If opts.cancel_flag is set mid-batch, remaining ops are not
 * attempted and the result vector is shorter than `ops`. Never throws.
 *
 * `cfg` provides the audit-log destination subfolder (cfg.junk_subfolder)
 * and is validated up front; ConfigError thrown on bad subfolder names
 * is the ONLY exception this function emits. */
constexpr int kMaxCollisionSuffix = 999;

std::vector<MoveResult> ExecuteMoves(
    const std::vector<MoveOp>& ops,
    const std::filesystem::path& root,
    const Config& cfg,
    const ExecuteOptions& opts = {});

}  /* namespace triage */
