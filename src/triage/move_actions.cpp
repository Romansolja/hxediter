#include "triage/move_actions.h"

#include "path_utils.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace triage {

namespace {

namespace fs = std::filesystem;

/* Convert a UTF-8 std::string from FileVerdict::path back into a fs::path.
 * Mirrors PathFromUtf8 in the existing path_utils, but we want a path
 * specifically (not the wide string), so we go through the fs::u8path
 * helper. C++17's fs::u8path takes a const char* / std::string. */
fs::path PathFromVerdict(const std::string& utf8) {
    return fs::u8path(utf8);
}

/* Compose the destination path: <root>/<bucket>/<src-rel>.
 * Returns {} if src is not under root (which would mean the scanner
 * gave us a path outside its own walk — shouldn't happen, but defensive). */
fs::path ComposeDst(const fs::path& root,
                    const std::string& bucket,
                    const fs::path& src) {
    std::error_code ec;
    fs::path rel = fs::relative(src, root, ec);
    if (ec || rel.empty() || *rel.begin() == "..") {
        return {};  /* signal failure */
    }
    return root / bucket / rel;
}

/* Insert a " (N)" suffix before the extension. "report.pdf" + 3 ->
 * "report (3).pdf"; "noext" + 1 -> "noext (1)". */
fs::path AddCollisionSuffix(const fs::path& dst, int n) {
    fs::path stem = dst.stem();
    fs::path ext  = dst.extension();
    fs::path parent = dst.parent_path();
    std::ostringstream oss;
    oss << stem.string() << " (" << n << ")" << ext.string();
    return parent / oss.str();
}

/* Best-effort "starts_with" for fs::path on C++17. */
bool PathStartsWith(const fs::path& candidate, const fs::path& prefix) {
    auto cit = candidate.begin();
    auto pit = prefix.begin();
    for (; pit != prefix.end(); ++cit, ++pit) {
        if (cit == candidate.end()) return false;
        if (*cit != *pit) return false;
    }
    return true;
}

std::string IsoTimestampLocal() {
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
    oss << std::put_time(&tm, "%Y-%m-%dT%H-%M-%S");
    return oss.str();
}

}  /* namespace */

std::vector<MoveOp> PlanAutoMoves(
    const std::vector<FileVerdict>& verdicts,
    const Config& cfg,
    const fs::path& root) {
    ValidateConfig(cfg);

    std::vector<MoveOp> ops;
    ops.reserve(verdicts.size());
    for (const FileVerdict& fv : verdicts) {
        const std::string* bucket = nullptr;
        switch (fv.verdict) {
            case Verdict::Junk:      bucket = &cfg.junk_subfolder;       break;
            case Verdict::Empty:     bucket = &cfg.junk_subfolder;       break;
            case Verdict::Duplicate: bucket = &cfg.duplicates_subfolder; break;
            case Verdict::Unknown:   bucket = &cfg.review_subfolder;     break;
            case Verdict::Useful:    /* skip */                          break;
            case Verdict::Error:     /* skip */                          break;
        }
        if (!bucket) continue;

        fs::path src = PathFromVerdict(fv.path);
        fs::path dst = ComposeDst(root, *bucket, src);
        if (dst.empty()) continue;  /* src not under root — skip silently */
        ops.push_back({std::move(src), std::move(dst), fv.verdict});
    }
    return ops;
}

std::vector<MoveOp> PlanBucketMoves(
    const std::vector<FileVerdict>& verdicts,
    const std::vector<std::size_t>& selected_indices,
    const std::string& bucket_name,
    const fs::path& root) {
    if (!IsValidSubfolderName(bucket_name)) {
        throw ConfigError("PlanBucketMoves: invalid bucket_name \"" +
                          bucket_name + "\"");
    }
    std::vector<MoveOp> ops;
    ops.reserve(selected_indices.size());
    for (std::size_t i : selected_indices) {
        if (i >= verdicts.size()) continue;
        const FileVerdict& fv = verdicts[i];
        if (fv.verdict == Verdict::Error) continue;  /* never auto-move errors */

        fs::path src = PathFromVerdict(fv.path);
        fs::path dst = ComposeDst(root, bucket_name, src);
        if (dst.empty()) continue;
        ops.push_back({std::move(src), std::move(dst), fv.verdict});
    }
    return ops;
}

std::vector<MoveResult> ExecuteMoves(
    const std::vector<MoveOp>& ops,
    const fs::path& root,
    const Config& cfg,
    const ExecuteOptions& opts) {
    /* Validate cfg early — same throws-ConfigError contract as the
     * planning entry points. Don't move a single file with a bogus
     * audit log path. */
    ValidateConfig(cfg);

    std::vector<MoveResult> out;
    out.reserve(ops.size());

    std::error_code ec;
    fs::path can_root = fs::weakly_canonical(root, ec);
    if (ec) can_root = root;  /* fall back; guard below uses lexical match */

    for (const MoveOp& op : ops) {
        /* Honour cancellation — same shape as scanner's per-file check.
         * Stops between ops; the in-flight rename always completes
         * (filesystem ops are atomic by design). */
        if (opts.cancel_flag && opts.cancel_flag->load(std::memory_order_relaxed)) {
            break;
        }
        MoveResult r;
        r.op = op;

        /* 1. Path-escape guard. weakly_canonical works on non-existent
         * paths in C++17, which is exactly what we need (dst doesn't
         * exist yet). */
        std::error_code ec_dst;
        fs::path can_dst = fs::weakly_canonical(op.dst, ec_dst);
        if (ec_dst) can_dst = op.dst;

        if (!PathStartsWith(can_dst, can_root)) {
            r.status        = MoveStatus::Rejected;
            r.error_message = "destination escapes scan root";
            out.push_back(std::move(r));
            continue;
        }

        /* 2. Lazy-create parent. */
        std::error_code ec_mk;
        fs::create_directories(op.dst.parent_path(), ec_mk);
        if (ec_mk) {
            r.status        = MoveStatus::Failed;
            r.error_message = "create_directories: " + ec_mk.message();
            out.push_back(std::move(r));
            continue;
        }

        /* 3. Resolve collision: walk suffixes until a free name appears. */
        fs::path final_dst = op.dst;
        if (fs::exists(final_dst, ec_mk)) {
            int n = 1;
            for (; n <= kMaxCollisionSuffix; ++n) {
                fs::path candidate = AddCollisionSuffix(op.dst, n);
                if (!fs::exists(candidate, ec_mk)) {
                    final_dst = candidate;
                    break;
                }
            }
            if (n > kMaxCollisionSuffix) {
                r.status = MoveStatus::Rejected;
                r.error_message = "collision suffix exhausted (>999 dups)";
                out.push_back(std::move(r));
                continue;
            }
        }

        /* 4. Rename, with cross-device-link fallback to copy+remove. */
        std::error_code ec_mv;
        fs::rename(op.src, final_dst, ec_mv);
        if (ec_mv) {
            const auto cross = std::make_error_code(std::errc::cross_device_link);
            if (ec_mv == cross) {
                std::error_code ec_cp;
                fs::copy_file(op.src, final_dst,
                              fs::copy_options::overwrite_existing, ec_cp);
                if (ec_cp) {
                    r.status        = MoveStatus::Failed;
                    r.error_message = "copy_file (cross-device): " + ec_cp.message();
                    out.push_back(std::move(r));
                    continue;
                }
                std::error_code ec_rm;
                fs::remove(op.src, ec_rm);
                if (ec_rm) {
                    /* Copy succeeded but original couldn't be removed —
                     * partial success. Treat as Failed; the user can
                     * delete the leftover original manually. */
                    r.status        = MoveStatus::Failed;
                    r.error_message = "remove (after cross-device copy): " +
                                      ec_rm.message();
                    r.final_dst     = final_dst;
                    out.push_back(std::move(r));
                    continue;
                }
            } else {
                r.status        = MoveStatus::Failed;
                r.error_message = "rename: " + ec_mv.message();
                out.push_back(std::move(r));
                continue;
            }
        }

        r.status    = MoveStatus::Moved;
        r.final_dst = final_dst;
        out.push_back(std::move(r));
    }

    /* Audit log: only written if anything was attempted. Destination
     * is always <root>/<cfg.junk_subfolder>/ regardless of which
     * bucket(s) this batch touched — one canonical location across
     * runs, co-located with where users look first. Lazy-creates
     * the junk subfolder if no Junk move ran in this batch (rare
     * edge case: e.g. a move-only-Duplicates batch with no Junk). */
    if (opts.write_audit_log && !out.empty()) {
        std::error_code ec_logdir;
        fs::path log_dir = root / cfg.junk_subfolder;
        fs::create_directories(log_dir, ec_logdir);
        if (!ec_logdir) {
            fs::path log_path = log_dir / ("triage-log-" + IsoTimestampLocal() + ".txt");
            std::ofstream log(log_path);
            if (log) {
                log << "# triage audit log " << IsoTimestampLocal() << "\n";
                log << "# status|src|dst|reason|error\n";
                for (const MoveResult& r : out) {
                    const char* sk =
                        r.status == MoveStatus::Moved    ? "MOVED"    :
                        r.status == MoveStatus::Failed   ? "FAILED"   :
                        r.status == MoveStatus::Rejected ? "REJECTED" : "?";
                    log << sk << "|"
                        << PathToGenericUtf8(r.op.src) << "|"
                        << (r.status == MoveStatus::Moved
                              ? PathToGenericUtf8(r.final_dst)
                              : PathToGenericUtf8(r.op.dst)) << "|"
                        << VerdictName(r.op.bucket_reason) << "|"
                        << r.error_message << "\n";
                }
            }
        }
    }

    return out;
}

}  /* namespace triage */
