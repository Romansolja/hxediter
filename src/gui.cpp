/* gui.cpp — Dear ImGui hex editor frame entry.
 *
 * All rendering for the individual UI regions (toolbar, hex grid,
 * status bar, start screen, help panel) now lives under src/ui/.
 * This file is the thin frame-shell that wires them together plus
 * the global keyboard-shortcut handler. */

#include "gui.h"
#include "platform.h"
#include "app_state.h"

#include "ui/actions.h"
#include "ui/gui_state.h"
#include "ui/help_panel.h"
#include "ui/hex_grid.h"
#include "ui/layout.h"
#include "ui/start_screen.h"
#include "ui/status_bar.h"
#include "ui/theme.h"
#include "ui/toolbar.h"

#include "imgui.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

/* Single owner of all persistent editor UI state. Renderers receive it
 * by reference instead of reading file-scope globals. */
ui::GuiState g_state;

/* ------------------------------------------------------------------ */
/* Accessibility helpers                                              */
/* ------------------------------------------------------------------ */
void CyclePalette(ui::GuiState& s) {
    int next = ((int)s.palette + 1) % (int)ui::GuiState::PAL_COUNT;
    s.palette = (ui::GuiState::Palette)next;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Palette: %s", ui::theme::Name(s.palette));
    s.SetStatus(buf, ui::GuiState::STATUS_INFO);
}

void AdjustFontScale(ui::GuiState& s, float delta) {
    float v = s.font_scale + delta;
    if (v < ui::layout::kFontScaleMin) v = ui::layout::kFontScaleMin;
    if (v > ui::layout::kFontScaleMax) v = ui::layout::kFontScaleMax;
    s.font_scale = v;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Font scale: %d%%", (int)(v * 100.0f + 0.5f));
    s.SetStatus(buf, ui::GuiState::STATUS_INFO);
}

/* ------------------------------------------------------------------ */
/* Keyboard shortcuts                                                 */
/* ------------------------------------------------------------------ */
void HandleShortcuts(ui::GuiState& s, HexEditorCore& core) {
    ImGuiIO& io = ImGui::GetIO();

    /* F1 toggles the help panel even when a text field is focused. */
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) s.show_help = !s.show_help;

    /* Dismiss the help panel on any click that wasn't consumed by a
     * widget (so clicking the toolbar `?` button or a hex byte still
     * works as expected). Gated on show_help so this is a no-op once
     * the panel is already hidden. */
    if (s.show_help &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() &&
        !ImGui::IsAnyItemActive()) {
        s.show_help = false;
    }

    if (io.WantTextInput) return;

    const bool ctrl  = io.KeyCtrl;
    const bool shift = io.KeyShift;

    /* Undo + pagination (unchanged from the original handler). */
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) ui::DoUndo(s, core);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))  { if (core.PageNext()) s.MarkInteracted(); }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))    { if (core.PagePrev()) s.MarkInteracted(); }

    /* Font scale */
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))) {
        AdjustFontScale(s, +ui::layout::kFontScaleStep);
    }
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))) {
        AdjustFontScale(s, -ui::layout::kFontScaleStep);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_0)) {
        s.font_scale = 1.0f;
        s.SetStatus("Font scale: 100%", ui::GuiState::STATUS_INFO);
    }

    /* Palette cycle (Ctrl+Shift+P) */
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_P)) {
        CyclePalette(s);
    }

    /* Byte-grid arrow navigation.
     *
     * Guards:
     *   - no inline edit is active (selected_byte < 0)
     *   - ImGui's keyboard nav highlight isn't currently on a widget
     *     (io.NavVisible) — otherwise Tab-focused buttons would eat
     *     the same arrow press and we'd double-move. */
    if (s.selected_byte < 0 && !io.NavVisible) {
        const int64_t size = core.GetFileSize();
        const int64_t bpl  = BYTES_PER_LINE;
        if (size > 0) {
            auto ensure_visible = [&](int64_t caret) {
                const int64_t page_start = core.GetCurrentOffset();
                if (caret < page_start ||
                    caret >= page_start + (int64_t)PAGE_SIZE) {
                    core.GoToOffset(caret);
                }
            };
            auto step = [&](int64_t delta) {
                if (s.caret_byte < 0) s.caret_byte = core.GetCurrentOffset();
                int64_t next = s.caret_byte + delta;
                if (next < 0)        next = 0;
                if (next > size - 1) next = size - 1;
                s.caret_byte = next;
                s.MarkInteracted();
                ensure_visible(s.caret_byte);
            };
            if      (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  step(-1);
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step(+1);
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    step(-bpl);
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  step(+bpl);
            else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
                if (s.caret_byte < 0) s.caret_byte = core.GetCurrentOffset();
                s.caret_byte -= (s.caret_byte % bpl);
                s.MarkInteracted();
                ensure_visible(s.caret_byte);
            } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
                if (s.caret_byte < 0) s.caret_byte = core.GetCurrentOffset();
                int64_t row_end = s.caret_byte - (s.caret_byte % bpl) + bpl - 1;
                if (row_end > size - 1) row_end = size - 1;
                s.caret_byte = row_end;
                s.MarkInteracted();
                ensure_visible(s.caret_byte);
            } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                /* Start editing the byte under the caret. */
                if (s.caret_byte >= 0 && !core.IsReadOnly()) {
                    s.selected_byte = s.caret_byte;
                    s.edit_buf[0]   = '\0';
                    s.focus_edit    = true;
                    s.focus_field   = ui::GuiState::FOCUS_BYTE;
                    s.MarkInteracted();
                }
            }
        }
    }
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Public entry                                                       */
/* ------------------------------------------------------------------ */
void SetEditorFonts(ImFont* ui_font, ImFont* mono_font,
                    ImFont* title_font, ImFont* icon_font) {
    g_state.ui_font    = ui_font;
    g_state.mono_font  = mono_font;
    g_state.title_font = title_font;
    g_state.icon_font  = icon_font;
}

void RenderHexEditorUI(AppState state,
                       HexEditorCore* core,
                       const char* load_error,
                       std::string* out_pending_path) {
    auto& s = g_state;

    /* Session-only font scale is applied every frame. Reset to 1.0f on
     * each launch — persistence is intentionally out of scope. */
    ImGui::GetIO().FontGlobalScale = s.font_scale;

    /* Smooth the help-panel visibility toward its target. */
    float dt = ImGui::GetIO().DeltaTime;
    float t  = dt * ui::layout::kHelpAnimSpeed;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float help_target = s.show_help ? 1.0f : 0.0f;
    s.help_anim = s.help_anim + (help_target - s.help_anim) * t;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    std::string title = core ? ("hxediter — " + core->GetFilename())
                              : std::string("hxediter");
    ImGui::Begin(title.c_str(), nullptr, flags);

    /* Palette drives every renderer via const reference. Style colors
     * and style vars that were pushed inline in the old gui.cpp now
     * live in theme::PushEditorStyle. */
    const auto& pal = ui::theme::Active(s.palette);
    ui::theme::PushEditorStyle(pal);

    /* Reset focus tracker each frame; toolbar/grid re-set it via
     * IsItemActive checks so the contextual hint can follow keyboard
     * focus. */
    s.focus_field = (s.selected_byte >= 0) ? ui::GuiState::FOCUS_BYTE
                                           : ui::GuiState::FOCUS_NONE;

    if (state == AppState::StartScreen) {
        ui::RenderStartScreen(s, pal, load_error, out_pending_path);
        ui::theme::PopEditorStyle();
        ImGui::End();
        return;
    }

    /* Invariant: state == HexView implies core != nullptr (guaranteed
     * by the main loop's load-completion block — a failed load resets
     * state back to StartScreen before this function runs). */
    HexEditorCore& core_ref = *core;

    /* Poll the file mtime/size baseline. Once we notice drift we latch
     * the flag until the user resolves it; we don't clear it on our own,
     * because the baseline would immediately rebase if we called reload
     * without confirmation. Cheap: a single _stat64 per frame on local FS.
     *
     * The transient warning is sticky so the user doesn't miss it if they
     * happen to be looking at a different part of the screen when the
     * external write lands. They dismiss it by clicking the 'x' next to
     * the badge; the separate red EXTERNALLY MODIFIED indicator stays up
     * until the edit/undo conflict modal actually resolves the state. */
    if (!s.externally_modified && core_ref.HasExternalModification()) {
        s.externally_modified = true;
        s.SetStatus("File changed on disk", ui::GuiState::STATUS_WARN, true);
    }

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    ui::RenderToolbar(s, pal, core_ref);
    if (s.ui_font) ImGui::PopFont();
    ImGui::Separator();

    /* Layout is shared between the header strip (rendered in its own
     * child so it stays put while the body scrolls) and the grid body
     * (rendered inside the ##hexview child). Zero left padding on both
     * children keeps SameLine(x) calls landing on the same pixels. */
    if (s.mono_font) ImGui::PushFont(s.mono_font);
    ui::HexLayout layout = ui::ComputeHexLayout(ImGui::GetContentRegionAvail().x);

    /* Header and body MUST share the same window.Pos.x, otherwise the
     * ImGui::SameLine(absolute_x) calls in RenderHexHeader and
     * RenderHexGrid land at different pixels and the byte columns
     * drift out from under the header labels. See the comment block
     * in the original gui.cpp for the full SameLine math. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
    ImGui::BeginChild("##hexheader",
                      ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ui::RenderHexHeader(pal, layout);
    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
    ImGui::BeginChild("##hexview",
                      ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      false,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    ui::RenderHexGrid(s, pal, core_ref, layout);
    ImGui::EndChild();
    if (s.mono_font) ImGui::PopFont();

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    ui::RenderStatusBar(s, pal, core_ref);
    if (s.ui_font) ImGui::PopFont();

    /* --- External-modification resolution modal ---
     *
     * Opens when either (a) the user tried to commit an edit while the
     * externally_modified flag was set, or (b) they clicked the red
     * "EXTERNALLY MODIFIED" badge in the status bar. Three exits:
     *
     *   Reload from disk — core.ReloadFromDisk(): refresh size, drop
     *                      undo stack, rebase baseline, discard any
     *                      pending edit. Safest option; it's the
     *                      default focused button.
     *   Keep my edits    — clears the flag and commits the pending edit
     *                      (if any). "Overwrite anyway" semantics.
     *   Cancel           — closes the modal, leaves the flag set,
     *                      discards the pending edit. User can keep
     *                      browsing; the next edit will re-trigger.
     */
    if (s.conflict_modal_open) {
        ImGui::OpenPopup("File changed on disk##conflict");
        s.conflict_modal_open = false;  /* one-shot trigger */
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("File changed on disk##conflict",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped(
            "Another program has written to this file since hxediter "
            "opened it. Undo history may no longer line up with what's "
            "on disk.");
        ImGui::Spacing();
        ImGui::TextWrapped("What would you like to do?");
        ImGui::Spacing();

        if (ImGui::Button("Reload from disk", ImVec2(160, 0))) {
            if (core_ref.ReloadFromDisk()) {
                s.externally_modified    = false;
                s.pending_edit_offset    = -1;
                s.SetStatus("Reloaded from disk", ui::GuiState::STATUS_OK);
            } else {
                s.SetStatus("Reload failed", ui::GuiState::STATUS_ERROR);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Keep my edits", ImVec2(140, 0))) {
            s.externally_modified = false;
            if (s.pending_edit_offset >= 0) {
                auto res = core_ref.EditByte(s.pending_edit_offset,
                                             s.pending_edit_value);
                if (res) {
                    char buf[80];
                    std::snprintf(buf, sizeof(buf),
                        "Edited 0x%" PRIX64 ": 0x%02X -> 0x%02X",
                        (uint64_t)res->offset, res->old_val, res->new_val);
                    s.SetStatus(buf, ui::GuiState::STATUS_OK);
                } else {
                    s.SetStatus("Edit failed (read-only?)",
                                ui::GuiState::STATUS_ERROR);
                }
                s.pending_edit_offset = -1;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            s.pending_edit_offset = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    HandleShortcuts(s, core_ref);

    ui::theme::PopEditorStyle();
    ImGui::End();
}
