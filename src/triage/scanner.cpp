#include "triage/scanner.h"

#include "triage/classifier.h"
#include "triage/hasher.h"
#include "path_utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace triage {

namespace fs = std::filesystem;

const char* ScanStateName(ScanState s) {
    switch (s) {
        case ScanState::Idle:        return "Idle";
        case ScanState::Walking:     return "Walking";
        case ScanState::Classifying: return "Classifying";
        case ScanState::Hashing:     return "Hashing";
        case ScanState::Done:        return "Done";
        case ScanState::Cancelled:   return "Cancelled";
        case ScanState::Failed:      return "Failed";
    }
    return "?";
}

namespace {

/* Global single-instance state.
 *
 * One scan at a time per process. The token mechanism prevents a stale
 * outer thread (slow to die after RequestCancel) from writing into
 * g_progress after a fresh StartScan has reset it. Mirrors the
 * abandon-flag pattern in updater.cpp. */
std::mutex                  g_mx;
ScanProgress                g_progress;
std::atomic<bool>           g_cancel{false};
std::atomic<int>            g_token{0};
std::condition_variable     g_done_cv;

template <typename F>
void WithSnap(F&& f) {
    std::lock_guard<std::mutex> lk(g_mx);
    f(g_progress);
}

bool IsScanRunningLocked() {
    return g_progress.state == ScanState::Walking
        || g_progress.state == ScanState::Classifying
        || g_progress.state == ScanState::Hashing;
}

bool IsTerminalLocked() {
    return g_progress.state == ScanState::Idle
        || g_progress.state == ScanState::Done
        || g_progress.state == ScanState::Cancelled
        || g_progress.state == ScanState::Failed;
}

int ResolveThreadCount(const Config& cfg) {
    int n = cfg.worker_threads;
    if (n <= 0) n = static_cast<int>(std::thread::hardware_concurrency());
    if (n < 1)  n = 1;
    return n;
}

/* Walk `root` once, collecting (path, size) pairs for every regular
 * file under it, EXCEPT files inside top-level subdirectories whose
 * names match the configured triage subfolders. The skip is scoped to
 * direct children of root only — a deeply-nested user folder named
 * "_junk" survives. See plan threading-model section. */
struct WalkedFile {
    fs::path     path;
    std::uint64_t size = 0;
};

bool WalkRoot(const fs::path& root,
              const Config& cfg,
              std::vector<WalkedFile>& out,
              const std::atomic<bool>& cancel,
              std::string& err_out) {
    /* Filesystem-aware comparison via PlatformBasenameEquals — `_Junk`
     * and `_junk` MUST match on NTFS or a renamed bucket would be
     * re-scanned next run. std::set<std::string> uses byte equality
     * which is wrong on Windows; iterate the three candidates and call
     * the helper. Three entries; not worth a fancier container. */
    const std::string buckets[] = {
        cfg.junk_subfolder,
        cfg.review_subfolder,
        cfg.duplicates_subfolder,
    };
    auto is_bucket_at_top = [&](const std::string& name) {
        for (const std::string& b : buckets) {
            if (PlatformBasenameEquals(name, b)) return true;
        }
        return false;
    };

    /* Predicate handed to ExpandDirectoryInto for the deep walk: prune
     * the subtree when the directory's basename is on the regenerable
     * list (.git, .venv, node_modules, __pycache__). Applied at every
     * depth — files inside these never enter the verdict table.
     * Bucket folders (`_junk`, `_review`, `_duplicates`) are NOT in
     * this predicate; they're skipped only at the top level (deeply
     * nested user folders happening to share these names survive). */
    auto skip_subtree = [](const fs::path& p) {
        return IsKnownJunkFolderBasename(p);
    };

    std::error_code ec;
    fs::directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        err_out = "directory_iterator(root): " + ec.message();
        return false;
    }

    fs::directory_iterator end;
    while (it != end) {
        if (cancel.load(std::memory_order_relaxed)) return true;
        std::error_code ec_k;
        const auto& entry = *it;
        std::error_code ec_isdir;
        const bool is_dir  = entry.is_directory(ec_isdir);
        std::error_code ec_isreg;
        const bool is_reg  = entry.is_regular_file(ec_isreg);

        if (!ec_isdir && is_dir) {
            const std::string name = entry.path().filename().string();
            const bool top_skip   = is_bucket_at_top(name);
            const bool junk_skip  = IsKnownJunkFolderBasename(entry.path());
            if (!top_skip && !junk_skip) {
                std::vector<std::string> sub_paths;
                ExpandDirectoryInto(entry.path(), sub_paths, skip_subtree);
                for (const std::string& s : sub_paths) {
                    if (cancel.load(std::memory_order_relaxed)) return true;
                    fs::path p = PathFromUtf8(s);
                    std::error_code ec_sz;
                    const auto sz = fs::file_size(p, ec_sz);
                    out.push_back({std::move(p),
                                   ec_sz ? 0 : static_cast<std::uint64_t>(sz)});
                }
            }
        } else if (!ec_isreg && is_reg) {
            std::error_code ec_sz;
            const auto sz = entry.file_size(ec_sz);
            out.push_back({entry.path(),
                           ec_sz ? 0 : static_cast<std::uint64_t>(sz)});
        }

        it.increment(ec_k);
        if (ec_k) break;  /* iteration error — stop, take what we have */
    }
    return true;
}

/* Phase: classify. Parallel over `walked`; serializes the per-file
 * callback so the consumer doesn't need its own mutex. */
void ClassifyPhase(const std::vector<WalkedFile>& walked,
                   const Config& cfg,
                   std::vector<FileVerdict>& out_verdicts,
                   const FileVerdictCallback& on_classified,
                   const std::atomic<bool>& cancel,
                   int my_token) {
    out_verdicts.assign(walked.size(), FileVerdict{});
    std::atomic<std::size_t> next_idx{0};
    std::mutex callback_mx;

    const int n_threads = ResolveThreadCount(cfg);
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(n_threads));

    for (int t = 0; t < n_threads; ++t) {
        pool.emplace_back([&]() {
            auto last_path_update = std::chrono::steady_clock::now()
                                  - std::chrono::milliseconds(60);
            while (true) {
                if (cancel.load(std::memory_order_relaxed)) return;
                if (g_token.load() != my_token) return;
                const std::size_t i = next_idx.fetch_add(1);
                if (i >= walked.size()) return;

                FileVerdict v = ClassifyFile(walked[i].path, cfg, walked[i].size);

                if (on_classified) {
                    std::lock_guard<std::mutex> lk(callback_mx);
                    on_classified(v);
                }

                out_verdicts[i] = std::move(v);

                /* Throttle current_path updates so the snapshot mutex
                 * isn't slammed during a million-file scan. Always bump
                 * the count, but only refresh the path string every
                 * 50 ms per worker. */
                const auto now = std::chrono::steady_clock::now();
                if (now - last_path_update > std::chrono::milliseconds(50)) {
                    WithSnap([&](ScanProgress& s) {
                        s.current_path     = PathToGenericUtf8(walked[i].path);
                        s.files_classified = s.files_classified + 1;
                    });
                    last_path_update = now;
                } else {
                    WithSnap([](ScanProgress& s) {
                        s.files_classified = s.files_classified + 1;
                    });
                }
            }
        });
    }
    for (auto& t : pool) t.join();
}

/* Phase: duplicate detection. Two-pass:
 *   1. Group all candidates by exact size; drop singletons.
 *   2. Hash first 64 KiB of remaining candidates.
 *   3. For groups where any member > 64 KiB and head-hashes coincide,
 *      hash full file to confirm/split. (Smaller files: head-hash IS
 *      full hash, so no rehash needed.)
 *   4. Pick canonical (lex-smallest path) per group; assign Duplicate
 *      verdict to non-canonical members EXCEPT those already classified
 *      Junk via known-junk basename — basename wins per rule 1.
 */
void HashPhase(std::vector<FileVerdict>& verdicts,
               std::vector<DupGroup>& out_groups,
               const Config& cfg,
               const FileVerdictUpdateCallback& on_dup_updated,
               const std::atomic<bool>& cancel,
               int my_token) {
    /* 1. Size grouping. Skip Empty/Error and zero-byte to avoid
     * pathological huge groups of zero-byte files (Empty files don't
     * benefit from dedup since there's no content). */
    std::unordered_map<std::uint64_t, std::vector<std::int32_t>> by_size;
    for (std::size_t i = 0; i < verdicts.size(); ++i) {
        const FileVerdict& v = verdicts[i];
        if (v.verdict == Verdict::Empty || v.verdict == Verdict::Error) continue;
        if (v.size == 0) continue;
        by_size[v.size].push_back(static_cast<std::int32_t>(i));
    }
    std::vector<std::int32_t> candidates;
    for (auto& [sz, idxs] : by_size) {
        (void)sz;
        if (idxs.size() < 2) continue;
        for (auto i : idxs) candidates.push_back(i);
    }

    WithSnap([&](ScanProgress& s) {
        s.total_to_hash = candidates.size();
    });

    if (candidates.empty()) return;

    /* 2. Head-hash all candidates in parallel. */
    auto run_hash_pool = [&](const std::vector<std::int32_t>& work,
                             std::uint64_t max_bytes,
                             bool count_progress) {
        std::atomic<std::size_t> next{0};
        const int n_threads = ResolveThreadCount(cfg);
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(n_threads));
        for (int t = 0; t < n_threads; ++t) {
            pool.emplace_back([&]() {
                while (true) {
                    if (cancel.load(std::memory_order_relaxed)) return;
                    if (g_token.load() != my_token) return;
                    const std::size_t k = next.fetch_add(1);
                    if (k >= work.size()) return;
                    const std::int32_t vi = work[k];
                    fs::path p = PathFromUtf8(verdicts[vi].path);
                    HashResult r = HashFile(p, max_bytes);
                    if (r.ok) verdicts[vi].content_hash = r.hash;
                    if (count_progress) {
                        WithSnap([](ScanProgress& s) {
                            s.files_hashed = s.files_hashed + 1;
                        });
                    }
                }
            });
        }
        for (auto& t : pool) t.join();
    };

    run_hash_pool(candidates, kHashHeadBytes, /*count_progress=*/true);
    if (cancel.load(std::memory_order_relaxed)) return;

    /* 3. Disambiguate large-file head-hash collisions with a full hash. */
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<std::int32_t>> head_groups;
    for (auto vi : candidates) {
        head_groups[{verdicts[vi].size, verdicts[vi].content_hash}].push_back(vi);
    }
    std::vector<std::int32_t> rehash;
    for (auto& [key, idxs] : head_groups) {
        if (idxs.size() < 2) continue;
        if (key.first > kHashHeadBytes) {
            for (auto i : idxs) rehash.push_back(i);
        }
    }
    if (!rehash.empty()) {
        /* Make the rehash work visible in the GUI/CLI progress bar.
         * Bump total_to_hash to (candidates.size() + rehash.size())
         * BEFORE the second pass starts; files_hashed continues
         * incrementing past the head-pass count during the rehash.
         * Without this the user sees "hashed N/N" while the disk
         * grinds for another minute on full-file rehashes of large
         * candidate groups. */
        WithSnap([&](ScanProgress& s) {
            s.total_to_hash = static_cast<std::uint64_t>(
                candidates.size() + rehash.size());
        });
        run_hash_pool(rehash, kHashFullFile, /*count_progress=*/true);
        if (cancel.load(std::memory_order_relaxed)) return;

        /* Recompute groupings now that large files have full hashes. */
        head_groups.clear();
        for (auto vi : candidates) {
            head_groups[{verdicts[vi].size, verdicts[vi].content_hash}].push_back(vi);
        }
    }

    /* 4. Build dup_groups, pick canonical (lex-smallest path), promote
     * non-canonical verdicts to Duplicate (unless rule 1 already won). */
    for (auto& [key, idxs] : head_groups) {
        if (idxs.size() < 2) continue;
        std::sort(idxs.begin(), idxs.end(),
                  [&](std::int32_t a, std::int32_t b) {
                      return verdicts[a].path < verdicts[b].path;
                  });
        DupGroup g;
        g.size = key.first;
        g.hash = key.second;
        g.verdict_indices = idxs;
        const std::int32_t group_id = static_cast<std::int32_t>(out_groups.size());

        for (std::size_t j = 0; j < idxs.size(); ++j) {
            const std::int32_t vi = idxs[j];
            verdicts[vi].dup_group     = group_id;
            verdicts[vi].dup_canonical = (j == 0);

            /* Rule-1 precedence: known-junk BASENAME stays Junk regardless
             * of dup status (the dup metadata is still attached for
             * inspection). Rule-5 Junk ("unmatched + small") DOES get
             * promoted — checking the verdict alone can't tell rule 1
             * from rule 5 since both produce Verdict::Junk, so we re-test
             * the basename here. Costs a 5-entry strcmp per dup. */
            if (j != 0) {
                fs::path p = PathFromUtf8(verdicts[vi].path);
                if (!IsKnownJunkBasename(p)) {
                    verdicts[vi].verdict = Verdict::Duplicate;
                    verdicts[vi].reason  =
                        "duplicate of " + verdicts[idxs[0]].path;
                }
            }
        }
        out_groups.push_back(std::move(g));
    }

    if (on_dup_updated) {
        for (const DupGroup& g : out_groups) {
            for (auto vi : g.verdict_indices) {
                on_dup_updated(verdicts[vi]);
            }
        }
    }
}

void ScanWorker(fs::path root,
                Config cfg,
                int my_token,
                FileVerdictCallback on_classified,
                FileVerdictUpdateCallback on_dup_updated) {
    auto token_alive = [my_token]() {
        return g_token.load() == my_token && !g_cancel.load();
    };
    auto fail = [my_token](std::string msg) {
        if (g_token.load() != my_token) return;
        WithSnap([&](ScanProgress& s) {
            s.state         = ScanState::Failed;
            s.error_message = std::move(msg);
        });
        g_done_cv.notify_all();
    };
    auto cancel_now = [my_token]() {
        if (g_token.load() != my_token) return;
        WithSnap([](ScanProgress& s) { s.state = ScanState::Cancelled; });
        g_done_cv.notify_all();
    };
    auto done_now = [my_token]() {
        if (g_token.load() != my_token) return;
        WithSnap([](ScanProgress& s) { s.state = ScanState::Done; });
        g_done_cv.notify_all();
    };

    /* ===== Walk phase ===== */
    std::vector<WalkedFile> walked;
    {
        std::string err;
        if (!WalkRoot(root, cfg, walked, g_cancel, err)) {
            fail(std::move(err));
            return;
        }
    }
    if (!token_alive()) { cancel_now(); return; }

    WithSnap([&](ScanProgress& s) {
        s.files_walked = walked.size();
        s.state        = ScanState::Classifying;
    });

    /* ===== Classify phase ===== */
    std::vector<FileVerdict> verdicts;
    ClassifyPhase(walked, cfg, verdicts, on_classified, g_cancel, my_token);
    if (!token_alive()) { cancel_now(); return; }

    /* Publish classify-phase results (without dup metadata yet). */
    WithSnap([&](ScanProgress& s) {
        s.files = verdicts;
        s.state = ScanState::Hashing;
    });

    /* ===== Hash phase ===== */
    std::vector<DupGroup> dup_groups;
    if (cfg.enable_duplicates) {
        HashPhase(verdicts, dup_groups, cfg, on_dup_updated, g_cancel, my_token);
        if (!token_alive()) { cancel_now(); return; }
    }

    WithSnap([&](ScanProgress& s) {
        s.files      = verdicts;
        s.dup_groups = std::move(dup_groups);
    });

    done_now();
}

}  /* namespace */

void StartScan(const fs::path& root,
               const Config& cfg,
               FileVerdictCallback on_classified,
               FileVerdictUpdateCallback on_dup_updated) {
    ValidateConfig(cfg);  /* throws ConfigError on bad subfolder names */

    int my_token;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (IsScanRunningLocked()) return;  /* one-at-a-time, silent no-op */
        g_cancel.store(false);
        my_token = g_token.fetch_add(1) + 1;
        g_progress        = ScanProgress{};
        g_progress.state  = ScanState::Walking;
        g_progress.root   = root;
        g_progress.config = cfg;  /* snapshot for panel / auditing */
    }

    std::thread([root, cfg, my_token,
                 cb_cls = std::move(on_classified),
                 cb_dup = std::move(on_dup_updated)]() {
        ScanWorker(root, cfg, my_token, std::move(cb_cls), std::move(cb_dup));
    }).detach();
}

ScanProgress GetProgress() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_progress;  /* copy under lock */
}

void RequestCancel() {
    g_cancel.store(true);
    g_done_cv.notify_all();  /* wake any WaitForCompletion early */
}

void Reset() {
    /* Increment the token first so any in-flight worker checking
     * g_token != my_token will treat itself as orphaned and not write
     * back into g_progress. Then take the lock, reset state, drop. */
    g_token.fetch_add(1);
    g_cancel.store(false);
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_progress = ScanProgress{};
    }
    g_done_cv.notify_all();
}

void WaitForCompletion() {
    std::unique_lock<std::mutex> lk(g_mx);
    g_done_cv.wait(lk, []() { return IsTerminalLocked(); });
}

}  /* namespace triage */
