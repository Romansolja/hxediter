/* hxsort — headless folder-triage CLI.
 *
 * Same hxcore classification engine as the in-editor triage panel. Two
 * output modes:
 *   default  — human-readable table to stdout, prints after scan ends.
 *   --json   — JSONL: one meta line, one verdict line per file (streamed
 *              as classified), one summary line at end. Streaming so a
 *              2 M-file scan doesn't buffer 400 MB before emit.
 *
 * Two action modes:
 *   --dry-run (default) — classify and report, change nothing.
 *   --move              — after scan, route by verdict to the configured
 *                         subfolders (_junk/_review/_duplicates).
 *
 * Exit codes:
 *   0  clean / dry-run completed
 *   1  partial — some moves failed
 *   2  invalid args
 *   3  scan failed
 */

#include "triage/classifier.h"
#include "triage/move_actions.h"
#include "triage/scanner.h"
#include "path_utils.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

struct CliArgs {
    fs::path        root;
    triage::Config  cfg;
    bool            do_move      = false;   /* --move; default is dry-run */
    bool            json_output  = false;
};

void PrintHelp(std::FILE* out) {
    std::fprintf(out,
        "hxsort " APP_VERSION " — folder triage CLI\n\n"
        "Usage:\n"
        "  hxsort <root> [options]\n\n"
        "Options:\n"
        "  --move                 Actually move files (default: dry-run)\n"
        "  --dry-run              Print plan, change nothing (default)\n"
        "  --json                 Emit JSONL (meta + verdict-per-line + summary)\n"
        "  --threads N            Worker threads (default: hardware_concurrency)\n"
        "  --junk-max BYTES       Unmatched files <= this are Junk (default 4096)\n"
        "  --no-signatures        Disable signature classification\n"
        "  --no-dups              Disable duplicate detection\n"
        "  --target-junk NAME     Override _junk subfolder name (must be plain basename)\n"
        "  --target-review NAME   Override _review subfolder name\n"
        "  --target-dup NAME      Override _duplicates subfolder name\n"
        "  -h, --help             Show this help\n\n"
        "Verdicts: Useful, Junk, Duplicate, Unknown, Empty, Error.\n"
        "Move routing: Junk+Empty -> _junk, Duplicate -> _duplicates,\n"
        "              Unknown -> _review, Useful+Error -> not moved.\n\n"
        "See: https://github.com/Romansolja/hxediter\n");
}

/* True iff `s` parses as a non-negative integer that fits in uint64_t.
 * Rejects overflow (silent wrap is how `--junk-max 99999999999999999999`
 * would otherwise become some innocuous in-range value). */
bool ParseUint64(const char* s, std::uint64_t* out) {
    if (!s || !*s) return false;
    constexpr std::uint64_t kMax = static_cast<std::uint64_t>(-1);
    std::uint64_t v = 0;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(*p - '0');
        /* Overflow-safe shape: if v*10 + digit would exceed kMax, fail.
         * Rearranged to avoid the overflow it's checking for:
         *   v*10 + digit > kMax  <=>  v > (kMax - digit) / 10. */
        if (v > (kMax - digit) / 10) return false;
        v = v * 10 + digit;
    }
    *out = v;
    return true;
}

bool ParseInt(const char* s, int* out) {
    std::uint64_t v;
    if (!ParseUint64(s, &v)) return false;
    if (v > 2'000'000'000ULL) return false;
    *out = static_cast<int>(v);
    return true;
}

/* argv parser. Returns 0 on success, 2 on user error (with a printed
 * message), or returns +1 if --help was requested (caller prints help and
 * exits 0). */
int ParseArgs(int argc, char** argv, CliArgs* out, std::string* err) {
    if (argc < 2) {
        *err = "no scan root provided";
        return 2;
    }
    bool seen_root = false;

    auto need_arg = [&](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            *err = std::string("flag ") + flag + " requires a value";
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            return 1;
        } else if (std::strcmp(a, "--move") == 0) {
            out->do_move = true;
        } else if (std::strcmp(a, "--dry-run") == 0) {
            out->do_move = false;
        } else if (std::strcmp(a, "--json") == 0) {
            out->json_output = true;
        } else if (std::strcmp(a, "--no-signatures") == 0) {
            out->cfg.enable_signatures = false;
        } else if (std::strcmp(a, "--no-dups") == 0) {
            out->cfg.enable_duplicates = false;
        } else if (std::strcmp(a, "--threads") == 0) {
            const char* v = need_arg(i, a); if (!v) return 2;
            int n = 0;
            if (!ParseInt(v, &n) || n < 0) {
                *err = "--threads: must be non-negative integer";
                return 2;
            }
            out->cfg.worker_threads = n;
        } else if (std::strcmp(a, "--junk-max") == 0) {
            const char* v = need_arg(i, a); if (!v) return 2;
            std::uint64_t n = 0;
            if (!ParseUint64(v, &n)) {
                *err = "--junk-max: must be non-negative integer (bytes)";
                return 2;
            }
            out->cfg.junk_max_bytes = n;
        } else if (std::strcmp(a, "--target-junk") == 0) {
            const char* v = need_arg(i, a); if (!v) return 2;
            const std::string reason = triage::ValidateSubfolderName(v);
            if (!reason.empty()) {
                *err = std::string("--target-junk: \"") + v + "\": " + reason;
                return 2;
            }
            out->cfg.junk_subfolder = v;
        } else if (std::strcmp(a, "--target-review") == 0) {
            const char* v = need_arg(i, a); if (!v) return 2;
            const std::string reason = triage::ValidateSubfolderName(v);
            if (!reason.empty()) {
                *err = std::string("--target-review: \"") + v + "\": " + reason;
                return 2;
            }
            out->cfg.review_subfolder = v;
        } else if (std::strcmp(a, "--target-dup") == 0) {
            const char* v = need_arg(i, a); if (!v) return 2;
            const std::string reason = triage::ValidateSubfolderName(v);
            if (!reason.empty()) {
                *err = std::string("--target-dup: \"") + v + "\": " + reason;
                return 2;
            }
            out->cfg.duplicates_subfolder = v;
        } else if (a[0] == '-') {
            *err = std::string("unknown flag: ") + a;
            return 2;
        } else {
            if (seen_root) {
                *err = std::string("multiple roots provided: \"") + a + "\"";
                return 2;
            }
            out->root = PathFromUtf8(a);
            seen_root = true;
        }
    }

    if (!seen_root) {
        *err = "no scan root provided";
        return 2;
    }

    /* Final config validation (defense in depth on top of per-flag
     * IsValidSubfolderName checks above). */
    try {
        triage::ValidateConfig(out->cfg);
    } catch (const triage::ConfigError& e) {
        *err = e.what();
        return 2;
    }
    return 0;
}

/* JSONL serialization helpers. Defined here (CLI-only) to keep nlohmann
 * out of the hxcore static lib. */
json VerdictToJson(const triage::FileVerdict& v) {
    json j;
    j["path"]           = v.path;
    j["size"]           = v.size;
    j["verdict"]        = triage::VerdictName(v.verdict);
    j["signature_id"]   = v.signature_id;
    j["content_hash"]   = v.content_hash;
    j["dup_group"]      = v.dup_group;
    j["dup_canonical"]  = v.dup_canonical;
    j["reason"]         = v.reason;
    return j;
}

json ConfigToJson(const triage::Config& c) {
    json j;
    j["enable_signatures"]    = c.enable_signatures;
    j["enable_duplicates"]    = c.enable_duplicates;
    j["junk_max_bytes"]       = c.junk_max_bytes;
    j["worker_threads"]       = c.worker_threads;
    j["junk_subfolder"]       = c.junk_subfolder;
    j["review_subfolder"]     = c.review_subfolder;
    j["duplicates_subfolder"] = c.duplicates_subfolder;
    return j;
}

std::string IsoTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    ::localtime_s(&tm, &t);
#else
    ::localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

struct SummaryCounts {
    std::uint64_t total = 0;
    std::uint64_t useful = 0;
    std::uint64_t junk = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t unknown = 0;
    std::uint64_t empty = 0;
    std::uint64_t error = 0;
    std::uint64_t reclaimable_bytes = 0;  /* sum of Junk + Duplicate + Empty */
};

SummaryCounts ComputeSummary(const std::vector<triage::FileVerdict>& v) {
    SummaryCounts s;
    s.total = v.size();
    for (const auto& fv : v) {
        switch (fv.verdict) {
            case triage::Verdict::Useful:    ++s.useful;    break;
            case triage::Verdict::Junk:      ++s.junk;      s.reclaimable_bytes += fv.size; break;
            case triage::Verdict::Duplicate: ++s.duplicate; s.reclaimable_bytes += fv.size; break;
            case triage::Verdict::Unknown:   ++s.unknown;   break;
            case triage::Verdict::Empty:     ++s.empty;     s.reclaimable_bytes += fv.size; break;
            case triage::Verdict::Error:     ++s.error;     break;
        }
    }
    return s;
}

json SummaryToJson(const SummaryCounts& s) {
    json j;
    j["total"]             = s.total;
    j["useful"]            = s.useful;
    j["junk"]              = s.junk;
    j["duplicate"]         = s.duplicate;
    j["unknown"]           = s.unknown;
    j["empty"]             = s.empty;
    j["error"]             = s.error;
    j["reclaimable_bytes"] = s.reclaimable_bytes;
    return j;
}

/* Format a byte count as a short human-readable string ("482 MB",
 * "12 KB", "47 B"). Uses 1024-base (KiB family) but emits short suffix
 * since that's what most folder-size users see in their file managers. */
std::string FormatBytes(std::uint64_t b) {
    constexpr std::uint64_t kKiB = 1024ULL;
    constexpr std::uint64_t kMiB = kKiB * 1024;
    constexpr std::uint64_t kGiB = kMiB * 1024;
    char buf[32];
    if (b >= kGiB) {
        std::snprintf(buf, sizeof(buf), "%.1f GB",
                      static_cast<double>(b) / kGiB);
    } else if (b >= kMiB) {
        std::snprintf(buf, sizeof(buf), "%.1f MB",
                      static_cast<double>(b) / kMiB);
    } else if (b >= kKiB) {
        std::snprintf(buf, sizeof(buf), "%.1f KB",
                      static_cast<double>(b) / kKiB);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(b));
    }
    return buf;
}

/* Print the human-readable verdict table after the scan finishes. Paths
 * are made root-relative for readability; absolute paths are kept in
 * the JSONL output for unambiguous machine consumption. */
void PrintHumanTable(const std::vector<triage::FileVerdict>& verdicts,
                     const fs::path& root) {
    const std::string root_abs = PathToGenericUtf8(root);

    auto rel_path = [&](const std::string& p) -> std::string {
        if (p.size() > root_abs.size() &&
            p.compare(0, root_abs.size(), root_abs) == 0 &&
            (p[root_abs.size()] == '/' || p[root_abs.size()] == '\\')) {
            return p.substr(root_abs.size() + 1);
        }
        return p;
    };

    std::printf("%-10s %10s %-8s %s\n", "VERDICT", "SIZE", "SIG", "PATH");
    std::printf("%-10s %10s %-8s %s\n", "-------", "----", "---", "----");
    for (const auto& v : verdicts) {
        std::printf("%-10s %10s %-8s %s\n",
            triage::VerdictName(v.verdict),
            FormatBytes(v.size).c_str(),
            v.signature_id.empty() ? "-" : v.signature_id.c_str(),
            rel_path(v.path).c_str());
    }
}

}  /* namespace */

int main(int argc, char** argv) {
    CliArgs args;
    std::string err;
    int rc = ParseArgs(argc, argv, &args, &err);
    if (rc == 1) {  /* --help */
        PrintHelp(stdout);
        return 0;
    }
    if (rc == 2) {
        std::fprintf(stderr, "hxsort: %s\n", err.c_str());
        std::fprintf(stderr, "Run `hxsort --help` for usage.\n");
        return 2;
    }

    std::error_code ec;
    if (!fs::exists(args.root, ec) || !fs::is_directory(args.root, ec)) {
        std::fprintf(stderr,
            "hxsort: root \"%s\" is not a directory\n",
            PathToGenericUtf8(args.root).c_str());
        return 3;
    }

    /* Streaming JSONL: emit meta line up front; install per-file callback
     * that emits a verdict line as each file is classified. After scan,
     * emit dup-update lines for promoted Duplicate records. Final summary
     * line. The mutex serializes stdout writes from the worker callbacks
     * even though the scanner already serializes its callback invocations
     * (defense in depth — std::cout is not guaranteed line-atomic on all
     * platforms when multiple threads write to the same stream). */
    std::mutex stdout_mx;

    if (args.json_output) {
        json meta = {
            {"meta", {
                {"root",          PathToGenericUtf8(args.root)},
                {"config",        ConfigToJson(args.cfg)},
                {"started_at",    IsoTimestamp()},
                {"hxsort_version", APP_VERSION},
            }},
        };
        std::cout << meta.dump() << "\n";
        std::cout.flush();
    }

    /* JSONL streaming: every per-line emit explicitly flushes so that a
     * tail-f'd or jq-piped consumer sees verdicts as the scanner finishes
     * them, not when 4-64 KB of stdout buffer happens to fill. The
     * extra flush per line is cheap relative to the work that produced
     * the verdict, and it's the entire point of the streaming claim in
     * the docstring. */
    triage::FileVerdictCallback on_classified = nullptr;
    triage::FileVerdictUpdateCallback on_dup_updated = nullptr;
    if (args.json_output) {
        on_classified = [&](const triage::FileVerdict& v) {
            std::lock_guard<std::mutex> lk(stdout_mx);
            std::cout << VerdictToJson(v).dump() << "\n";
            std::cout.flush();
        };
        on_dup_updated = [&](const triage::FileVerdict& v) {
            std::lock_guard<std::mutex> lk(stdout_mx);
            json j = VerdictToJson(v);
            j["dup_update"] = true;  /* flag so consumers can dedup against
                                        the original line if they wish    */
            std::cout << j.dump() << "\n";
            std::cout.flush();
        };
    }

    triage::StartScan(args.root, args.cfg,
                      std::move(on_classified),
                      std::move(on_dup_updated));
    triage::WaitForCompletion();

    triage::ScanProgress final_state = triage::GetProgress();

    if (final_state.state == triage::ScanState::Failed) {
        std::fprintf(stderr, "hxsort: scan failed: %s\n",
                     final_state.error_message.c_str());
        return 3;
    }
    if (final_state.state == triage::ScanState::Cancelled) {
        std::fprintf(stderr, "hxsort: scan cancelled\n");
        return 3;
    }

    SummaryCounts summary = ComputeSummary(final_state.files);

    if (args.json_output) {
        json sj = {{"summary", SummaryToJson(summary)}};
        std::cout << sj.dump() << "\n";
        std::cout.flush();
    } else {
        PrintHumanTable(final_state.files, args.root);
        std::printf("\n%llu files: %llu useful, %llu junk, %llu duplicate, "
                    "%llu unknown, %llu empty, %llu error. %s reclaimable.\n",
            (unsigned long long)summary.total,
            (unsigned long long)summary.useful,
            (unsigned long long)summary.junk,
            (unsigned long long)summary.duplicate,
            (unsigned long long)summary.unknown,
            (unsigned long long)summary.empty,
            (unsigned long long)summary.error,
            FormatBytes(summary.reclaimable_bytes).c_str());
    }

    /* Move phase, only when --move and not --dry-run. */
    if (args.do_move) {
        std::vector<triage::MoveOp> ops;
        try {
            ops = triage::PlanAutoMoves(final_state.files, args.cfg, args.root);
        } catch (const triage::ConfigError& e) {
            std::fprintf(stderr, "hxsort: %s\n", e.what());
            return 2;
        }

        /* Audit-log destination is now derived from `cfg.junk_subfolder`
         * inside ExecuteMoves — see move_actions.h ExecuteOptions
         * docstring. The CLI passes args.cfg here directly. */
        triage::ExecuteOptions exec_opts;
        std::vector<triage::MoveResult> results =
            triage::ExecuteMoves(ops, args.root, args.cfg, exec_opts);

        std::uint64_t moved = 0, failed = 0, rejected = 0;
        for (const auto& r : results) {
            switch (r.status) {
                case triage::MoveStatus::Moved:    ++moved; break;
                case triage::MoveStatus::Failed:   ++failed; break;
                case triage::MoveStatus::Rejected: ++rejected; break;
            }
            if (!args.json_output) {
                if (r.status == triage::MoveStatus::Moved) {
                    std::printf("MOVED  %s -> %s\n",
                                PathToGenericUtf8(r.op.src).c_str(),
                                PathToGenericUtf8(r.final_dst).c_str());
                } else {
                    std::printf("FAILED %s -> %s  (%s)\n",
                                PathToGenericUtf8(r.op.src).c_str(),
                                PathToGenericUtf8(r.op.dst).c_str(),
                                r.error_message.c_str());
                }
            } else {
                json mj = {
                    {"move", {
                        {"src",     PathToGenericUtf8(r.op.src)},
                        {"dst",     r.status == triage::MoveStatus::Moved
                                        ? PathToGenericUtf8(r.final_dst)
                                        : PathToGenericUtf8(r.op.dst)},
                        {"status",  r.status == triage::MoveStatus::Moved    ? "Moved"
                                  : r.status == triage::MoveStatus::Failed   ? "Failed"
                                  : "Rejected"},
                        {"reason",  triage::VerdictName(r.op.bucket_reason)},
                        {"error",   r.error_message},
                    }},
                };
                std::lock_guard<std::mutex> lk(stdout_mx);
                std::cout << mj.dump() << "\n";
                std::cout.flush();
            }
        }
        if (!args.json_output) {
            std::printf("\n%llu moved, %llu failed, %llu rejected.\n",
                (unsigned long long)moved,
                (unsigned long long)failed,
                (unsigned long long)rejected);
        }
        if (failed > 0 || rejected > 0) return 1;
    }

    return 0;
}
