#include "triage/scanner.h"

#include "triage/classifier.h"
#include "triage/hasher.h"
#include "path_utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
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

/* Single-instance scanner state. One scan at a time per process. The
 * token mechanism prevents a stale outer thread (slow to die after
 * RequestCancel) from writing into the progress snapshot after a fresh
 * StartScan has reset it.
 *
 * State lives on the heap, owned by g_state below. Worker threads
 * capture a copy of the shared_ptr by value when they spawn — that copy
 * keeps the struct alive even after main() returns. Without this, an
 * in-flight ScanWorker (mid-walk on a huge tree, or mid-hash on a large
 * file) would lock a destroyed mutex and signal a destroyed condvar
 * once main returns. Same fix applied in updater.cpp.
 *
 * `active_workers` lets Reset() wait for in-flight outer workers to
 * acknowledge the cancel/token-bump and exit before returning. Without
 * it, a fast caller doing Reset() → StartScan() in immediate succession
 * could have two zombie workers in the air at once: the pre-Reset one
 * (still mid-walk, will exit on next token check) and the post-Start
 * one (fresh token). Both are correct in isolation thanks to the token
 * check, but the reviewer's read was that we relied on it by accident
 * rather than by design — make the wait explicit. */
struct ScannerState {
    std::mutex                  mx;
    ScanProgress                progress;
    std::atomic<bool>           cancel{false};
    std::atomic<int>            token{0};
    std::condition_variable     done_cv;
    std::atomic<int>            active_workers{0};
    std::condition_variable     worker_exit_cv;
};

std::shared_ptr<ScannerState> g_state = std::make_shared<ScannerState>();

template <typename F>
void WithSnap(ScannerState& st, F&& f) {
    std::lock_guard<std::mutex> lk(st.mx);
    f(st.progress);
}

bool IsScanRunningLocked(ScannerState& st) {
    return st.progress.state == ScanState::Walking
        || st.progress.state == ScanState::Classifying
        || st.progress.state == ScanState::Hashing;
}

bool IsTerminalLocked(ScannerState& st) {
    return st.progress.state == ScanState::Idle
        || st.progress.state == ScanState::Done
        || st.progress.state == ScanState::Cancelled
        || st.progress.state == ScanState::Failed;
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
void ClassifyPhase(ScannerState& st,
                   const std::vector<WalkedFile>& walked,
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
                if (st.token.load() != my_token) return;
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
                    WithSnap(st, [&](ScanProgress& s) {
                        s.current_path     = PathToGenericUtf8(walked[i].path);
                        s.files_classified = s.files_classified + 1;
                    });
                    last_path_update = now;
                } else {
                    WithSnap(st, [](ScanProgress& s) {
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
void HashPhase(ScannerState& st,
               std::vector<FileVerdict>& verdicts,
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

    WithSnap(st, [&](ScanProgress& s) {
        s.total_to_hash = candidates.size();
    });

    if (candidates.empty()) return;

    /* 2. Head-hash all candidates in parallel.
     *
     * On HashFile failure (vanished file, ACL flipped post-walk, flaky
     * media), promote the verdict to Error and clear content_hash. The
     * grouping pass below skips Error verdicts so unreadable files don't
     * land in a (size, 0) bucket and get nuked into _duplicates/. */
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
                    if (st.token.load() != my_token) return;
                    const std::size_t k = next.fetch_add(1);
                    if (k >= work.size()) return;
                    const std::int32_t vi = work[k];
                    fs::path p = PathFromUtf8(verdicts[vi].path);
                    HashResult r = HashFile(p, max_bytes);
                    if (r.ok) {
                        verdicts[vi].content_hash = r.hash;
                    } else {
                        verdicts[vi].content_hash = 0;
                        verdicts[vi].verdict = Verdict::Error;
                        verdicts[vi].reason  =
                            "hash failed (file vanished or unreadable post-walk)";
                    }
                    if (count_progress) {
                        WithSnap(st, [](ScanProgress& s) {
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

    /* 3. Disambiguate large-file head-hash collisions with a full hash.
     *
     * Skip Verdict::Error candidates here — those are files whose hash
     * failed in step 2. Including them would lump every failed-hash file
     * into a single (size, 0) bucket and falsely call them duplicates of
     * each other; rule of thumb in this scanner: never group on a hash
     * we don't have. */
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<std::int32_t>> head_groups;
    for (auto vi : candidates) {
        if (verdicts[vi].verdict == Verdict::Error) continue;
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
        WithSnap(st, [&](ScanProgress& s) {
            s.total_to_hash = static_cast<std::uint64_t>(
                candidates.size() + rehash.size());
        });
        run_hash_pool(rehash, kHashFullFile, /*count_progress=*/true);
        if (cancel.load(std::memory_order_relaxed)) return;

        /* Recompute groupings now that large files have full hashes. */
        head_groups.clear();
        for (auto vi : candidates) {
            if (verdicts[vi].verdict == Verdict::Error) continue;
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

void ScanWorker(std::shared_ptr<ScannerState> state,
                fs::path root,
                Config cfg,
                int my_token,
                FileVerdictCallback on_classified,
                FileVerdictUpdateCallback on_dup_updated) {
    ScannerState& st = *state;
    /* Reset() waits on worker_exit_cv until active_workers == 0 so a
     * subsequent StartScan starts with no zombies in flight. RAII guard
     * to make the dec+notify happen on every exit path. */
    st.active_workers.fetch_add(1, std::memory_order_acq_rel);
    struct ExitGuard {
        ScannerState& st;
        ~ExitGuard() {
            st.active_workers.fetch_sub(1, std::memory_order_acq_rel);
            st.worker_exit_cv.notify_all();
        }
    } exit_guard{st};

    auto token_alive = [&st, my_token]() {
        return st.token.load() == my_token && !st.cancel.load();
    };
    auto fail = [&st, my_token](std::string msg) {
        if (st.token.load() != my_token) return;
        WithSnap(st, [&](ScanProgress& s) {
            s.state         = ScanState::Failed;
            s.error_message = std::move(msg);
        });
        st.done_cv.notify_all();
    };
    auto cancel_now = [&st, my_token]() {
        if (st.token.load() != my_token) return;
        WithSnap(st, [](ScanProgress& s) { s.state = ScanState::Cancelled; });
        st.done_cv.notify_all();
    };
    auto done_now = [&st, my_token]() {
        if (st.token.load() != my_token) return;
        WithSnap(st, [](ScanProgress& s) { s.state = ScanState::Done; });
        st.done_cv.notify_all();
    };

    /* ===== Walk phase ===== */
    std::vector<WalkedFile> walked;
    {
        std::string err;
        if (!WalkRoot(root, cfg, walked, st.cancel, err)) {
            fail(std::move(err));
            return;
        }
    }
    if (!token_alive()) { cancel_now(); return; }

    WithSnap(st, [&](ScanProgress& s) {
        s.files_walked = walked.size();
        s.state        = ScanState::Classifying;
    });

    /* ===== Classify phase ===== */
    std::vector<FileVerdict> verdicts;
    ClassifyPhase(st, walked, cfg, verdicts, on_classified, st.cancel, my_token);
    if (!token_alive()) { cancel_now(); return; }

    /* Publish classify-phase results (without dup metadata yet). */
    WithSnap(st, [&](ScanProgress& s) {
        s.files = verdicts;
        s.state = ScanState::Hashing;
    });

    /* ===== Hash phase ===== */
    std::vector<DupGroup> dup_groups;
    if (cfg.enable_duplicates) {
        HashPhase(st, verdicts, dup_groups, cfg, on_dup_updated, st.cancel, my_token);
        if (!token_alive()) { cancel_now(); return; }
    }

    WithSnap(st, [&](ScanProgress& s) {
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

    auto state = g_state;  /* keep state alive past main() return */
    ScannerState& st = *state;

    int my_token;
    {
        std::lock_guard<std::mutex> lk(st.mx);
        if (IsScanRunningLocked(st)) return;  /* one-at-a-time, silent no-op */
        st.cancel.store(false);
        my_token = st.token.fetch_add(1) + 1;
        st.progress        = ScanProgress{};
        st.progress.state  = ScanState::Walking;
        st.progress.root   = root;
        st.progress.config = cfg;  /* snapshot for panel / auditing */
    }

    std::thread([state, root, cfg, my_token,
                 cb_cls = std::move(on_classified),
                 cb_dup = std::move(on_dup_updated)]() {
        ScanWorker(state, root, cfg, my_token,
                   std::move(cb_cls), std::move(cb_dup));
    }).detach();
}

ScanProgress GetProgress() {
    ScannerState& st = *g_state;
    std::lock_guard<std::mutex> lk(st.mx);
    return st.progress;  /* copy under lock */
}

void RequestCancel() {
    ScannerState& st = *g_state;
    st.cancel.store(true);
    st.done_cv.notify_all();  /* wake any WaitForCompletion early */
}

void Reset() {
    ScannerState& st = *g_state;
    /* Increment the token first so any in-flight worker checking
     * st.token != my_token will treat itself as orphaned and not write
     * back into st.progress. Set cancel as well so the inner thread
     * pools (classify / hash) bail at their next per-file check
     * regardless of the token race window. */
    st.token.fetch_add(1);
    st.cancel.store(true);
    st.done_cv.notify_all();

    /* Wait briefly for outer workers (ScanWorker) to acknowledge.
     * Workers check the token between every per-file step, so the
     * window before they return is bounded by one classify or hash
     * iteration. A 500 ms timeout is plenty in normal operation; if
     * we hit it (ridiculously slow disk, OS-level I/O hang), we
     * proceed anyway — the orphan worker still respects the token
     * and won't corrupt the post-Reset state. */
    {
        std::unique_lock<std::mutex> lk(st.mx);
        st.worker_exit_cv.wait_for(
            lk, std::chrono::milliseconds(500),
            [&st]() { return st.active_workers.load() == 0; });
    }

    /* Now safe to reset state cleanly: any worker still alive past the
     * timeout will not write because of the bumped token. */
    st.cancel.store(false);
    {
        std::lock_guard<std::mutex> lk(st.mx);
        st.progress = ScanProgress{};
    }
    st.done_cv.notify_all();
}

void WaitForCompletion() {
    ScannerState& st = *g_state;
    std::unique_lock<std::mutex> lk(st.mx);
    st.done_cv.wait(lk, [&st]() { return IsTerminalLocked(st); });
}

}  /* namespace triage */
