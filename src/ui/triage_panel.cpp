#include "ui/triage_panel.h"

#include "triage/classifier.h"
#include "triage/move_actions.h"
#include "triage/scanner.h"

#include "path_utils.h"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ui {

namespace {

/* File-local move-thread state. Mirrors scanner.cpp's pattern: one move
 * batch at a time per process, a mutex around the result vector,
 * atomics for control:
 *   in_progress  — read by render to disable the Move buttons
 *   cancel       — set by Back / panel teardown; ExecuteMoves polls
 *                  this between ops and returns early
 *   token        — bumped on every StartMoveThread AND on every
 *                  RequestMoveCancelAndClearSummary; the detached
 *                  worker compares its captured my_token against the
 *                  current global before writing back its terminal
 *                  summary. Mismatch means the worker has been
 *                  replaced or invalidated and its results must be
 *                  silently dropped.
 *
 * The token is what fixes the race that "clear summary on Back" alone
 * cannot: a slow ExecuteMoves can take minutes to wind down (cross-
 * device copy of a 4 GB file completes mid-op even after cancel=true),
 * and without a token the worker's eventual summary write would
 * clobber the panel's empty summary AND surface stale text on a
 * subsequent unrelated scan. Same shape as scanner.cpp:64 g_token. */
std::mutex                                g_move_mx;
std::atomic<bool>                         g_move_in_progress{false};
std::atomic<bool>                         g_move_cancel{false};
std::atomic<int>                          g_move_token{0};
std::vector<triage::MoveResult>           g_move_results;
std::string                               g_move_summary;  /* short status line */

/* Called by the Back button. Signals the worker to stop after its
 * current op AND invalidates its eventual summary write (via token
 * bump) so the cleared summary stays cleared even after the worker
 * finishes whatever filesystem op it was mid-way through. Safe to
 * call when no move is in flight. */
void RequestMoveCancelAndClearSummary() {
    g_move_cancel.store(true);
    g_move_token.fetch_add(1);   /* orphan any in-flight worker */
    std::lock_guard<std::mutex> lk(g_move_mx);
    g_move_summary.clear();
    g_move_results.clear();
}

void StartMoveThread(std::vector<triage::MoveOp> ops,
                     std::filesystem::path root,
                     triage::Config cfg) {
    if (g_move_in_progress.load()) return;
    g_move_cancel.store(false);     /* fresh batch, fresh cancel state */
    /* Capture our token AFTER bumping it. Any worker that captured a
     * lower value is now considered stale and will skip its summary
     * write below. */
    const int my_token = g_move_token.fetch_add(1) + 1;
    g_move_in_progress.store(true);
    {
        std::lock_guard<std::mutex> lk(g_move_mx);
        g_move_results.clear();
        g_move_summary = "Moving...";
    }
    std::thread([ops  = std::move(ops),
                 root = std::move(root),
                 cfg  = std::move(cfg),
                 my_token]() mutable {
        triage::ExecuteOptions exec_opts;
        exec_opts.cancel_flag = &g_move_cancel;
        std::vector<triage::MoveResult> results =
            triage::ExecuteMoves(ops, root, cfg, exec_opts);

        std::uint64_t moved = 0, failed = 0, rejected = 0;
        for (const auto& r : results) {
            switch (r.status) {
                case triage::MoveStatus::Moved:    ++moved;    break;
                case triage::MoveStatus::Failed:   ++failed;   break;
                case triage::MoveStatus::Rejected: ++rejected; break;
            }
        }
        const bool was_cancelled = g_move_cancel.load();
        char summary[200];
        if (was_cancelled) {
            std::snprintf(summary, sizeof(summary),
                          "Cancelled after %llu / %llu moves",
                          (unsigned long long)moved,
                          (unsigned long long)ops.size());
        } else {
            std::snprintf(summary, sizeof(summary),
                          "Moved %llu of %llu (%llu failed, %llu rejected)",
                          (unsigned long long)moved,
                          (unsigned long long)results.size(),
                          (unsigned long long)failed,
                          (unsigned long long)rejected);
        }

        {
            std::lock_guard<std::mutex> lk(g_move_mx);
            /* Stale-thread guard: if our token no longer matches the
             * current global, RequestMoveCancelAndClearSummary or a
             * fresh StartMoveThread has fired since we started. Either
             * way the user does not want our terminal summary — keep
             * whatever the more-recent action set. We still flip
             * in_progress so a later StartMoveThread isn't blocked. */
            if (g_move_token.load() == my_token) {
                g_move_results = std::move(results);
                g_move_summary = summary;
            }
        }
        g_move_in_progress.store(false);
    }).detach();
}

const ImVec4 VerdictColor(triage::Verdict v) {
    switch (v) {
        case triage::Verdict::Useful:    return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
        case triage::Verdict::Junk:      return ImVec4(0.85f, 0.50f, 0.45f, 1.0f);
        case triage::Verdict::Duplicate: return ImVec4(0.85f, 0.70f, 0.45f, 1.0f);
        case triage::Verdict::Unknown:   return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        case triage::Verdict::Empty:     return ImVec4(0.55f, 0.55f, 0.75f, 1.0f);
        case triage::Verdict::Error:     return ImVec4(0.90f, 0.45f, 0.45f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

std::string FormatBytes(std::uint64_t b) {
    constexpr std::uint64_t kKiB = 1024ULL;
    constexpr std::uint64_t kMiB = kKiB * 1024;
    constexpr std::uint64_t kGiB = kMiB * 1024;
    char buf[32];
    if (b >= kGiB) std::snprintf(buf, sizeof(buf), "%.1f GB", (double)b / kGiB);
    else if (b >= kMiB) std::snprintf(buf, sizeof(buf), "%.1f MB", (double)b / kMiB);
    else if (b >= kKiB) std::snprintf(buf, sizeof(buf), "%.1f KB", (double)b / kKiB);
    else std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
    return buf;
}

/* Strip the common-prefix scan root so the table shows root-relative
 * paths. Falls back to the absolute path if the prefix doesn't match
 * (paranoid; shouldn't happen). */
std::string RelativePath(const std::string& abs_path,
                         const std::string& root_abs) {
    if (root_abs.empty()) return abs_path;
    if (abs_path.size() <= root_abs.size()) return abs_path;
    if (abs_path.compare(0, root_abs.size(), root_abs) != 0) return abs_path;
    std::size_t i = root_abs.size();
    while (i < abs_path.size() && (abs_path[i] == '/' || abs_path[i] == '\\')) ++i;
    return abs_path.substr(i);
}

bool MaskHas(std::uint8_t mask, triage::Verdict v) {
    return (mask & (1u << static_cast<unsigned>(v))) != 0;
}
void MaskToggle(std::uint8_t& mask, triage::Verdict v, bool on) {
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(v));
    if (on) mask = static_cast<std::uint8_t>(mask | bit);
    else    mask = static_cast<std::uint8_t>(mask & ~bit);
}

}  /* namespace */

void RenderTriagePanel(GuiState& s, bool* out_request_back) {
    const triage::ScanProgress prog = triage::GetProgress();
    const std::string root_abs = PathToGenericUtf8(prog.root);

    /* Resize selection vector to mirror the verdict list. New rows
     * default to unchecked; existing entries preserve their state. */
    if (s.triage_checked.size() != prog.files.size()) {
        s.triage_checked.resize(prog.files.size(), false);
    }

    /* ===== Header ===== */
    if (ImGui::Button("< Back")) {
        /* Cancel any in-flight move BEFORE we leave the panel — the
         * detached thread keeps churning otherwise, and a stale summary
         * would surface on the next scan's panel. RequestMoveCancel
         * also clears g_move_summary so an unrelated subsequent scan
         * doesn't display this batch's "Moved 234/5000" message. */
        RequestMoveCancelAndClearSummary();
        if (out_request_back) *out_request_back = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Root:");
    ImGui::SameLine();
    ImGui::TextUnformatted(root_abs.empty() ? "(none)" : root_abs.c_str());

    /* Phase + counts. */
    char phase_line[256];
    std::snprintf(phase_line, sizeof(phase_line),
                  "%s  |  walked: %llu  classified: %llu  hashed: %llu / %llu  files: %zu",
                  triage::ScanStateName(prog.state),
                  (unsigned long long)prog.files_walked,
                  (unsigned long long)prog.files_classified,
                  (unsigned long long)prog.files_hashed,
                  (unsigned long long)prog.total_to_hash,
                  prog.files.size());
    ImGui::TextUnformatted(phase_line);

    /* Cancel + Rescan controls. */
    const bool is_running = prog.state == triage::ScanState::Walking
                          || prog.state == triage::ScanState::Classifying
                          || prog.state == triage::ScanState::Hashing;
    if (is_running) {
        if (ImGui::Button("Cancel scan")) triage::RequestCancel();
    } else {
        if (ImGui::Button("Rescan") && !prog.root.empty()) {
            /* Reuse the SAME Config the original scan ran with — anything
             * else would change the meaning of Rescan (e.g. a junk-max
             * override would silently revert on the rescan). */
            triage::Config rescan_cfg = prog.config;
            triage::Reset();
            triage::StartScan(prog.root, rescan_cfg, nullptr, nullptr);
        }
    }

    /* Move-thread status line (if any). */
    if (g_move_in_progress.load()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(move in progress...)");
    } else {
        std::lock_guard<std::mutex> lk(g_move_mx);
        if (!g_move_summary.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", g_move_summary.c_str());
        }
    }

    ImGui::Separator();

    /* ===== Filter row ===== */
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##triage_filter", "Filter path...",
                             s.triage_filter_buf, sizeof(s.triage_filter_buf));
    ImGui::SameLine();
    auto verdict_checkbox = [&](const char* label, triage::Verdict v) {
        bool on = MaskHas(s.triage_filter_mask, v);
        if (ImGui::Checkbox(label, &on)) MaskToggle(s.triage_filter_mask, v, on);
        ImGui::SameLine();
    };
    verdict_checkbox("Useful",    triage::Verdict::Useful);
    verdict_checkbox("Junk",      triage::Verdict::Junk);
    verdict_checkbox("Duplicate", triage::Verdict::Duplicate);
    verdict_checkbox("Unknown",   triage::Verdict::Unknown);
    verdict_checkbox("Empty",     triage::Verdict::Empty);
    verdict_checkbox("Error",     triage::Verdict::Error);
    ImGui::NewLine();

    /* ===== Table ===== */
    /* Build the visible-row index list, applying mask + text filter. */
    std::vector<std::size_t> visible;
    visible.reserve(prog.files.size());
    const std::size_t filter_len = std::strlen(s.triage_filter_buf);
    for (std::size_t i = 0; i < prog.files.size(); ++i) {
        const triage::FileVerdict& fv = prog.files[i];
        if (!MaskHas(s.triage_filter_mask, fv.verdict)) continue;
        if (filter_len > 0) {
            if (fv.path.find(s.triage_filter_buf) == std::string::npos) continue;
        }
        visible.push_back(i);
    }

    /* Selected count (across all rows, not just visible). */
    std::size_t selected_count = 0;
    std::uint64_t selected_bytes = 0;
    for (std::size_t i = 0; i < s.triage_checked.size() && i < prog.files.size(); ++i) {
        if (s.triage_checked[i]) {
            ++selected_count;
            selected_bytes += prog.files[i].size;
        }
    }

    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg
                                          | ImGuiTableFlags_Borders
                                          | ImGuiTableFlags_ScrollY
                                          | ImGuiTableFlags_Resizable;

    /* Reserve space for the footer (counts + buttons). */
    const float footer_h = ImGui::GetFrameHeightWithSpacing() * 2 + 8.0f;
    ImVec2 table_size = ImGui::GetContentRegionAvail();
    table_size.y -= footer_h;
    if (table_size.y < 100.0f) table_size.y = 100.0f;

    if (ImGui::BeginTable("##triage_table", 6, table_flags, table_size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##sel",    ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableSetupColumn("Verdict",  ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Size",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Sig",      ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("Dup",      ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Path",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t i = visible[static_cast<std::size_t>(row)];
                const triage::FileVerdict& fv = prog.files[i];

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool checked = s.triage_checked[i];
                char chk_id[24]; std::snprintf(chk_id, sizeof(chk_id), "##t%zu", i);
                if (ImGui::Checkbox(chk_id, &checked)) {
                    s.triage_checked[i] = checked;
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(VerdictColor(fv.verdict), "%s",
                                   triage::VerdictName(fv.verdict));

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(FormatBytes(fv.size).c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(fv.signature_id.empty() ? "-"
                                                               : fv.signature_id.c_str());

                ImGui::TableSetColumnIndex(4);
                if (fv.dup_group >= 0) {
                    ImGui::Text("%d%s", fv.dup_group, fv.dup_canonical ? "*" : "");
                } else {
                    ImGui::TextUnformatted("-");
                }

                ImGui::TableSetColumnIndex(5);
                const std::string rel = RelativePath(fv.path, root_abs);
                ImGui::TextUnformatted(rel.c_str());
            }
        }
        ImGui::EndTable();
    }

    /* ===== Footer ===== */
    ImGui::Text("%zu of %zu visible | %zu selected (%s)",
                visible.size(), prog.files.size(),
                selected_count, FormatBytes(selected_bytes).c_str());

    /* Quick selection helpers. */
    if (ImGui::Button("Select all visible")) {
        for (std::size_t i : visible) s.triage_checked[i] = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear selection")) {
        std::fill(s.triage_checked.begin(), s.triage_checked.end(), false);
    }
    ImGui::SameLine();
    /* Verdict-driven shortcut: pre-select all junk + duplicate + empty. */
    if (ImGui::Button("Select all Junk/Dup/Empty")) {
        for (std::size_t i = 0; i < prog.files.size(); ++i) {
            const auto v = prog.files[i].verdict;
            s.triage_checked[i] = (v == triage::Verdict::Junk)
                                || (v == triage::Verdict::Duplicate)
                                || (v == triage::Verdict::Empty);
        }
    }

    /* Move buttons. Disabled when scan is in progress, no rows are
     * selected, or a previous move is still running. */
    const bool moves_blocked = is_running || g_move_in_progress.load() ||
                               selected_count == 0;
    auto move_button = [&](const char* label, int target) {
        ImGui::SameLine();
        ImGui::BeginDisabled(moves_blocked);
        if (ImGui::Button(label)) {
            s.triage_show_confirm   = true;
            s.triage_confirm_target = target;
        }
        ImGui::EndDisabled();
    };
    move_button("Move to _junk",       0);
    move_button("Move to _review",     1);
    move_button("Move to _duplicates", 2);

    /* ===== Confirm popup =====
     *
     * The bucket name is read from `prog.config` — the SAME Config the
     * scanner used. This guarantees "Move to _junk" lands in the
     * bucket the next scan will skip; if the user (or a future flag)
     * overrides the subfolder name, scan and panel agree because they
     * read from one source of truth instead of constructing default
     * Configs in two places. */
    if (s.triage_show_confirm) {
        ImGui::OpenPopup("Confirm move");
        s.triage_show_confirm = false;  /* one-shot */
    }
    if (ImGui::BeginPopupModal("Confirm move", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string& bucket =
            s.triage_confirm_target == 0 ? prog.config.junk_subfolder
          : s.triage_confirm_target == 1 ? prog.config.review_subfolder
          :                                prog.config.duplicates_subfolder;

        ImGui::Text("Move %zu file(s) (%s) into <root>/%s/ ?",
                    selected_count, FormatBytes(selected_bytes).c_str(),
                    bucket.c_str());
        ImGui::TextWrapped("Files keep their relative paths inside the bucket. "
                           "An audit log is written to %s/. "
                           "Reverse by dragging files back to their original "
                           "locations; nothing is deleted.",
                           prog.config.junk_subfolder.c_str());
        ImGui::Separator();
        if (ImGui::Button("Move", ImVec2(120, 0))) {
            std::vector<std::size_t> indices;
            indices.reserve(selected_count);
            for (std::size_t i = 0; i < s.triage_checked.size() &&
                                    i < prog.files.size(); ++i) {
                if (s.triage_checked[i]) indices.push_back(i);
            }
            try {
                std::vector<triage::MoveOp> ops = triage::PlanBucketMoves(
                    prog.files, indices, bucket, prog.root);
                StartMoveThread(std::move(ops), prog.root, prog.config);
                /* Clear selection so the user can re-pick after the move. */
                std::fill(s.triage_checked.begin(), s.triage_checked.end(), false);
            } catch (const triage::ConfigError& e) {
                s.SetStatus(std::string("Move failed: ") + e.what(),
                            GuiState::STATUS_ERROR, true);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  /* namespace ui */
