#pragma once

/* triage::classifier — central types for the folder-triage feature.
 *
 * Shared by the in-editor triage panel and the hxsort CLI. Both consume
 * the same FileVerdict stream, so any change to these types affects both.
 *
 * See plan: C:\Users\Admin\.claude\plans\we-need-a-way-cuddly-sparrow.md
 */

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace triage {

/* Verdict enum — the user-visible classification.
 *
 * Order matters for serialization: never renumber existing values. New
 * values append. Stable strings produced by VerdictName() below. */
enum class Verdict : std::uint8_t {
    Useful    = 0,  /* matched signature; not a non-canonical duplicate */
    Junk      = 1,  /* known-junk basename, OR unmatched + small        */
    Duplicate = 2,  /* non-canonical member of a dup group              */
    Unknown   = 3,  /* unmatched, larger than junk_max_bytes            */
    Empty     = 4,  /* size == 0 and basename is not known-junk         */
    Error     = 5,  /* unreadable / vanished mid-scan                   */
};

const char* VerdictName(Verdict v);  /* "Useful", "Junk", ... */

/* One row in the triage table; one JSONL line in --json output.
 *
 * Records of basename-junk files (e.g. duplicate Thumbs.db files) carry
 * BOTH dup_canonical/dup_group AND verdict==Junk. By design — see plan
 * verdict-policy rule 1 commentary. Read the verdict for what to do
 * with the file; read the dup metadata for what is byte-equal to what. */
struct FileVerdict {
    std::string   path;            /* UTF-8 absolute path                     */
    std::uint64_t size = 0;
    Verdict       verdict = Verdict::Unknown;
    std::string   signature_id;    /* e.g. "pdf", "pe", "" if no match        */
    std::uint64_t content_hash = 0;/* xxh3-64 over hashed bytes; 0 if unhashed*/
    std::int32_t  dup_group = -1;  /* index into ScanProgress::dup_groups     */
    bool          dup_canonical = false;
    std::string   reason;          /* one-line human explanation              */
};

/* Run-time configuration. The three subfolder-name fields MUST be plain
 * basenames; this is enforced by ValidateConfig() below and is what lets
 * the move-action's path-escape guard remain trivial. */
struct Config {
    bool          enable_signatures = true;
    bool          enable_duplicates = true;
    std::uint64_t junk_max_bytes    = 4 * 1024;  /* unmatched + <= this -> Junk */
    int           worker_threads    = 0;          /* 0 -> hardware_concurrency  */

    /* Move-target subfolder NAMES (not paths). Validated: non-empty, no
     * '/' or '\', not "." or "..", not absolute. Move destinations are
     * always <root>/<NAME>/<original-relative>. */
    std::string junk_subfolder       = "_junk";
    std::string review_subfolder     = "_review";
    std::string duplicates_subfolder = "_duplicates";
};

/* Thrown by ValidateConfig() / hxsort argv parsing on a bad subfolder
 * name. CLI catches this, prints message, exits 2. GUI shouldn't see it
 * because the GUI doesn't expose name overrides yet. */
struct ConfigError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/* Throws ConfigError on the first invalid field. Idempotent. The thrown
 * message includes the specific validation rule that failed (e.g.
 * "reserved Windows device name") so a CLI error or GUI status text
 * tells the user something actionable. */
void ValidateConfig(const Config& cfg);

/* Validate a subfolder name. Returns the empty string if `name` is a
 * plain basename safe for use under a scan root; returns a short
 * human-readable explanation otherwise (e.g. "reserved Windows device
 * name (CON, NUL, COM*, LPT*, etc.)"). Public so the CLI and GUI can
 * surface specific rejection reasons rather than a generic "not a
 * plain basename" string that lies about which rule fired.
 *
 * The string is stable (it does NOT include the bad value itself);
 * callers wrap it with their own context like
 *   "--target-junk \"<value>\": <reason>". */
std::string ValidateSubfolderName(const std::string& name);

/* Boolean wrapper for callers that just want pass/fail. */
inline bool IsValidSubfolderName(const std::string& name) {
    return ValidateSubfolderName(name).empty();
}

/* The single deliberate exception to "content over metadata" — exact
 * basename matches against this set produce Verdict::Junk regardless of
 * size (overrides Empty) and regardless of dup classification. Hardcoded
 * by design; not exposed in Config. See plan verdict-policy rule 1.
 *
 * Unicode: Icon\r is the legendary macOS folder-icon stub (basename ends
 * in a CR byte). Comparison is exact bytewise on the UTF-8 basename. */
extern const std::array<const char*, 5> kKnownJunkBasenames;

/* True iff path's basename matches kKnownJunkBasenames exactly. */
bool IsKnownJunkBasename(const std::filesystem::path& p);

/* Folder-level analogue: directory basenames whose entire subtree is
 * skipped during a triage scan — regenerable build / cache / package
 * directories. Files inside these never reach the classifier, so they
 * don't show up in the verdict table at all (vs. being classified as
 * Junk, which would still surface them and slow the walk). The skip
 * applies at every depth, not just at the scan root. Comparison goes
 * through PlatformBasenameEquals so `.Venv` matches on NTFS. */
extern const std::array<const char*, 4> kKnownJunkFolderBasenames;

/* True iff path's basename matches kKnownJunkFolderBasenames. */
bool IsKnownJunkFolderBasename(const std::filesystem::path& p);

/* Single-file synchronous classification. Reads up to the first
 * kSignaturePeekBytes bytes of the file for signature matching when
 * cfg.enable_signatures is on. Does NOT compute content_hash or
 * dup_group — those are filled by the scanner's hash phase. The verdict
 * returned here is provisional with respect to duplicates: a file the
 * scanner later identifies as a non-canonical duplicate gets its
 * verdict promoted to Verdict::Duplicate (unless it was Verdict::Junk
 * via known-basename, which wins).
 *
 * `known_size` lets the caller pass a size already retrieved from a
 * directory_entry::file_size() — at scale this avoids one extra stat
 * per file. Pass kSizeUnknown to ask ClassifyFile to stat itself.
 *
 * Errors (file vanished, permission denied) -> Verdict::Error with
 * a populated reason string. Never throws. */
constexpr std::uint64_t kSizeUnknown = static_cast<std::uint64_t>(-1);

FileVerdict ClassifyFile(const std::filesystem::path& p,
                         const Config& cfg,
                         std::uint64_t known_size = kSizeUnknown);

}  /* namespace triage */
