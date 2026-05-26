#include "gui.h"
#include "platform.h"
#include "app_state.h"
#include "path_utils.h"

#include "ui/actions.h"
#include "ui/gui_state.h"
#include "ui/help_panel.h"
#include "ui/hex_grid.h"
#include "ui/layout.h"
#include "ui/preferences.h"
#include "ui/scoped_tooltip.h"
#include "ui/settings_panel.h"
#include "ui/shortcuts.h"
#include "ui/start_screen.h"
#include "ui/status_bar.h"
#include "ui/theme.h"
#include "ui/toolbar.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

ui::GuiState g_state;

// ImGui BeginTabItem with an empty label is UB — fall back to a placeholder.
std::string TabLabelBase(const std::string& path) {
    std::string base = PathBasename(path);
    if (base.empty()) return "(unnamed)";
    return base;
}

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

// Unconditional: the Cmd/Ctrl gate prevents clash with typing in goto/search.
void HandleTabShortcuts(std::vector<OpenDocument>& docs,
                        int* active_doc,
                        std::vector<int>* out_close_indices) {
    ImGuiIO& io = ImGui::GetIO();
    const int n = (int)docs.size();
    if (n <= 0) return;

    // IsKeyPressed defaults to repeat=true so Cmd-hold-Tab still cycles.
    const bool shortcut = ui::ShortcutHeld(io);

    if (shortcut && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        int cur = (*active_doc >= 0 && *active_doc < n) ? *active_doc : 0;
        *active_doc = io.KeyShift ? ((cur - 1 + n) % n)
                                  : ((cur + 1) % n);
    }

    if (shortcut && ImGui::IsKeyPressed(ImGuiKey_W)) {
        int cur = *active_doc;
        if (cur >= 0 && cur < n && out_close_indices) {
            out_close_indices->push_back(cur);
        }
    }

    // Cmd/Ctrl+1..9 — jump to tab N (1-indexed).
    for (int i = 0; i < 9; ++i) {
        ImGuiKey k = (ImGuiKey)((int)ImGuiKey_1 + i);
        if (shortcut && !io.KeyShift && ImGui::IsKeyPressed(k)) {
            if (i < n) *active_doc = i;
        }
    }
}

void HandleShortcuts(ui::GuiState& s, ui::DocumentState& doc,
                     HexEditorCore& core,
                     const ui::HexLayout& layout,
                     float body_height_px) {
    ImGuiIO& io = ImGui::GetIO();

    // F1 must work even when a text field is focused — handled before the WantTextInput gate.
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) s.show_help = !s.show_help;

    if (s.show_help &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() &&
        !ImGui::IsAnyItemActive()) {
        s.show_help = false;
    }

    // Wheel zoom before the WantTextInput gate — mouse gestures shouldn't be eaten by focus.
    const bool shortcut = ui::ShortcutHeld(io);
    if (shortcut && io.MouseWheel != 0.0f) {
        AdjustFontScale(s, (io.MouseWheel > 0.0f ? +1.0f : -1.0f)
                              * ui::layout::kFontScaleStep);
        io.MouseWheel = 0.0f;
    }

    if (io.WantTextInput) return;

    const bool ctrl  = shortcut;
    const bool shift = io.KeyShift;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) ui::DoUndo(s, doc, core);

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

    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_P)) {
        CyclePalette(s);
    }

    // !NavVisible: avoid double-move from Tab-focused buttons. selected_byte gate: don't
    // navigate while an inline byte edit is open (user is typing two hex digits).
    if (doc.selected_byte < 0 && !io.NavVisible) {
        const int64_t size = core.GetFileSize();
        const int64_t bpl  = (layout.bytes_per_line > 0) ? layout.bytes_per_line
                                                          : 16;
        const float row_h = ImGui::GetTextLineHeightWithSpacing();
        const int visible_rows = (body_height_px > 0 && row_h > 0)
            ? std::max(1, (int)(body_height_px / row_h))
            : 24;
        if (size > 0) {
            auto step = [&](int64_t delta) {
                if (doc.caret_byte < 0) doc.caret_byte = 0;
                int64_t next = doc.caret_byte + delta;
                if (next < 0)        next = 0;
                if (next > size - 1) next = size - 1;
                doc.caret_byte            = next;
                doc.pending_scroll_offset = next;
                s.MarkInteracted();
            };
            if      (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  step(-1);
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step(+1);
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    step(-bpl);
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  step(+bpl);
            else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))   step(+(int64_t)visible_rows * bpl);
            else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))     step(-(int64_t)visible_rows * bpl);
            else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
                if (doc.caret_byte < 0) doc.caret_byte = 0;
                doc.caret_byte           -= (doc.caret_byte % bpl);
                doc.pending_scroll_offset = doc.caret_byte;
                s.MarkInteracted();
            } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
                if (doc.caret_byte < 0) doc.caret_byte = 0;
                int64_t row_end = doc.caret_byte - (doc.caret_byte % bpl) + bpl - 1;
                if (row_end > size - 1) row_end = size - 1;
                doc.caret_byte            = row_end;
                doc.pending_scroll_offset = row_end;
                s.MarkInteracted();
            } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
                if (doc.caret_byte >= 0 && !core.IsReadOnly()) {
                    doc.selected_byte = doc.caret_byte;
                    doc.edit_buf[0]   = '\0';
                    doc.focus_edit    = true;
                    doc.focus_field   = ui::GuiState::FOCUS_BYTE;
                    s.MarkInteracted();
                }
            }
        }
    }
}

// Replaces ImGui's TabListPopupButton (sideways triangle reads as illegible).
// Lists the loaded directory's files plus the currently-open tabs.
bool RenderTabDirDropdown(ui::GuiState& s,
                          std::vector<OpenDocument>& docs,
                          int* active_doc,
                          const std::vector<std::string>* directory_files,
                          const std::string* directory_label,
                          std::vector<std::string>* out_pending_paths,
                          bool* out_clear_directory) {
    const float size       = ImGui::GetFrameHeight();
    const bool  have_icons = (s.icon_font_small != nullptr);
    const float btn_w      = size + (have_icons ? size * 0.55f : 0.0f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tabdir", ImVec2(btn_w, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = 0;
    if      (active)  bg = IM_COL32(255, 255, 255, 34);
    else if (hovered) bg = IM_COL32(255, 255, 255, 20);
    if (bg != 0) {
        dl->AddRectFilled(pos, ImVec2(pos.x + btn_w, pos.y + size), bg, 4.0f);
    }

    const ImU32 glyph = hovered ? IM_COL32(215, 220, 228, 255)
                                : IM_COL32(168, 174, 182, 220);
    if (have_icons) {
        ImGui::PushFont(s.icon_font_small);
        const ImVec2 fsz = ImGui::CalcTextSize(ICON_FA_FOLDER_TREE);
        const ImVec2 fp(pos.x + (size - fsz.x) * 0.5f,
                        pos.y + (size - fsz.y) * 0.5f);
        dl->AddText(fp, glyph, ICON_FA_FOLDER_TREE);

        const ImVec2 csz = ImGui::CalcTextSize(ICON_FA_CHEVRON_DOWN);
        const ImVec2 cp(pos.x + size + (btn_w - size - csz.x) * 0.5f,
                        pos.y + (size - csz.y) * 0.5f + 1.0f);
        dl->AddText(cp, glyph, ICON_FA_CHEVRON_DOWN);
        ImGui::PopFont();
    } else {
        // Fallback glyph when icon font is missing.
        const float cx = pos.x + size * 0.5f;
        const float cy = pos.y + size * 0.5f;
        const float r  = size * 0.22f;
        for (int i = 0; i < 3; ++i) {
            float y = cy + (i - 1) * r * 0.7f;
            dl->AddLine(ImVec2(cx - r, y), ImVec2(cx + r, y), glyph, 1.5f);
        }
    }

    if (hovered) {
        ui::ScopedStyledTooltip tip;
        if (directory_files && !directory_files->empty() && directory_label &&
            !directory_label->empty()) {
            ImGui::Text("Files in %s — open tabs", directory_label->c_str());
        } else {
            ImGui::TextUnformatted("Open tabs");
        }
    }

    if (clicked) ImGui::OpenPopup("##tabdir_popup");

    bool any_action = false;
    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 0.0f),
                                        ImVec2(420.0f, FLT_MAX));
    if (ImGui::BeginPopup("##tabdir_popup")) {
        const bool have_dir = (directory_files && !directory_files->empty() &&
                               directory_label && !directory_label->empty());

        if (have_dir) {
            ImGui::TextDisabled("Files in %s", directory_label->c_str());
            ImGui::Separator();
            // ##suffix disambiguates duplicate basenames — same label triggers ImGui ID conflict.
            for (int i = 0; i < (int)directory_files->size(); ++i) {
                const std::string& path = (*directory_files)[i];
                std::string base = TabLabelBase(path);
                char label[280];
                std::snprintf(label, sizeof(label), "%s##dirf%d", base.c_str(), i);
                if (ImGui::Selectable(label) && out_pending_paths) {
                    out_pending_paths->push_back(path);
                    any_action = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
            }
        }

        if (!docs.empty()) {
            if (have_dir) {
                ImGui::Spacing();
                ImGui::Separator();
            }
            ImGui::TextDisabled("Open tabs");
            ImGui::Separator();
            for (int i = 0; i < (int)docs.size(); ++i) {
                const std::string fn = docs[i].core ? docs[i].core->GetFilename()
                                                    : std::string();
                std::string base = TabLabelBase(fn);
                char label[280];
                std::snprintf(label, sizeof(label), "%s##goto%d", base.c_str(), i);
                bool selected = (i == *active_doc);
                if (ImGui::Selectable(label, selected)) {
                    *active_doc = i;
                    any_action = true;
                }
                if (ImGui::IsItemHovered() && !fn.empty()) {
                    ImGui::SetTooltip("%s", fn.c_str());
                }
            }
        }

        if (have_dir) {
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Selectable("Close folder") && out_clear_directory) {
                *out_clear_directory = true;
                any_action = true;
            }
        }

        if (!have_dir && docs.empty()) {
            ImGui::TextDisabled("No files");
        }

        ImGui::EndPopup();
    }

    return any_action;
}

void RenderTabBar(ui::GuiState& s,
                  std::vector<OpenDocument>& docs,
                  int* active_doc,
                  std::vector<int>* out_close_indices,
                  const std::vector<std::string>* directory_files,
                  const std::string* directory_label,
                  std::vector<std::string>* out_pending_paths,
                  bool* out_clear_directory) {
    const int n = (int)docs.size();

    RenderTabDirDropdown(s, docs, active_doc, directory_files, directory_label,
                         out_pending_paths, out_clear_directory);

    ImGui::SameLine(0.0f, 10.0f);

    if (n <= 0) {
        ImGui::AlignTextToFramePadding();
        if (directory_files && !directory_files->empty()) {
            ImGui::TextDisabled("Pick a file from the folder \xE2\x86\x92");
        } else {
            ImGui::TextDisabled("No file open");
        }
        return;
    }

    // Force selection only on the frame after a programmatic change (Ctrl+Tab, tab close) —
    // every-frame SetSelected would override user clicks.
    const int programmatic_target =
        (*active_doc != s.last_tab_active_seen) ? *active_doc : -1;

    ImGuiTabBarFlags tab_flags =
        ImGuiTabBarFlags_Reorderable |
        ImGuiTabBarFlags_AutoSelectNewTabs |
        ImGuiTabBarFlags_FittingPolicyScroll;

    // ImGui's default selected/unselected tabs are near-identical — push a contrast palette.
    const auto& pal_tab = ui::theme::Active(s.palette);
    const ImVec4 tab_unsel    (pal_tab.btn_secondary.x * 0.55f,
                               pal_tab.btn_secondary.y * 0.55f,
                               pal_tab.btn_secondary.z * 0.55f, 1.0f);
    const ImVec4 tab_hover    = pal_tab.btn_secondary_hover;
    const ImVec4 tab_selected = pal_tab.btn_primary_hover;
    const ImVec4 tab_overline = pal_tab.nav_cursor;
    const ImVec4 tab_dim_sel  (pal_tab.btn_primary.x,
                               pal_tab.btn_primary.y,
                               pal_tab.btn_primary.z, 0.85f);

    ImGui::PushStyleColor(ImGuiCol_Tab,                  tab_unsel);
    ImGui::PushStyleColor(ImGuiCol_TabHovered,           tab_hover);
    ImGui::PushStyleColor(ImGuiCol_TabSelected,          tab_selected);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline,  tab_overline);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed,            tab_unsel);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,    tab_dim_sel);
    ImGui::PushStyleVar  (ImGuiStyleVar_TabRounding,     5.0f);

    if (!ImGui::BeginTabBar("##docs", tab_flags)) {
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(6);
        return;
    }

    for (int i = 0; i < n; ++i) {
        OpenDocument& d = docs[i];
        const std::string filename = d.core ? d.core->GetFilename() : std::string();
        std::string base = TabLabelBase(filename);

        // Disambiguate identical filenames from different directories.
        char label[260];
        std::snprintf(label, sizeof(label), "%s##tab%d", base.c_str(), i);

        // No UnsavedDocument flag: edits write straight to disk — undo count is not "unsaved".
        ImGuiTabItemFlags item_flags = 0;
        if (i == programmatic_target) {
            item_flags |= ImGuiTabItemFlags_SetSelected;
        }

        bool open = true;
        bool visible = ImGui::BeginTabItem(label, &open, item_flags);
        if (ImGui::IsItemHovered() && !filename.empty()) {
            ImGui::SetTooltip("%s", filename.c_str());
        }
        if (visible) {
            *active_doc = i;
            ImGui::EndTabItem();
        }
        if (!open && out_close_indices) {
            out_close_indices->push_back(i);
        }
    }

    ImGui::EndTabBar();
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(6);

    s.last_tab_active_seen = *active_doc;
}

}

void SetEditorFonts(ImFont* ui_font, ImFont* mono_font,
                    ImFont* title_font, ImFont* icon_font,
                    ImFont* icon_font_small) {
    g_state.ui_font         = ui_font;
    g_state.mono_font       = mono_font;
    g_state.title_font      = title_font;
    g_state.icon_font       = icon_font;
    g_state.icon_font_small = icon_font_small;
}

void SetStartupDuration(float duration_ms) {
    g_state.startup_duration_ms = duration_ms;
    g_state.startup_measured    = true;
}

void SetContentScale(float scale) {
    g_state.content_scale = (scale < 1.0f) ? 1.0f : scale;
}

void LoadGuiPreferences() {
    ui::LoadPreferences(g_state);
}

void SaveGuiPreferences() {
    ui::SavePreferences(g_state);
}

bool ReadonlyDefault() {
    return g_state.readonly_default;
}

bool BackgroundThrottle() {
    return g_state.background_throttle;
}

void SetExternalStatus(const std::string& msg, bool is_error) {
    g_state.SetStatus(msg, is_error ? ui::GuiState::STATUS_ERROR
                                    : ui::GuiState::STATUS_INFO);
}

void RenderHexEditorUI(AppState state,
                       std::vector<OpenDocument>* docs,
                       int* active_doc,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       int drag_over_state,
                       std::vector<int>* out_close_indices,
                       const std::vector<std::string>* directory_files,
                       const std::string* directory_label,
                       bool* out_clear_directory,
                       std::vector<std::string>* out_pending_directories) {
    auto& s = g_state;

    // Two independent zoom axes — chrome on the main window, hex grid on its children — both
    // applied per-window via SetWindowFontScale. FontGlobalScale stays 1.0.
    ImGui::GetIO().FontGlobalScale = 1.0f;

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

    // Fixed window id — otherwise ImGui restyles when the active filename changes.
    ImGui::Begin("##hxediter_main", nullptr, flags);

    ImGui::SetWindowFontScale(s.chrome_scale);

    const auto& pal = ui::theme::Active(s.palette);
    ui::theme::PushEditorStyle(pal);

    if (state == AppState::StartScreen) {
        ui::RenderStartScreen(s, pal, load_error, out_pending_paths,
                              drag_over_state, out_pending_directories);
        ui::theme::PopEditorStyle();
        ImGui::End();
        return;
    }

    // HexView is valid with empty docs as long as a folder is loaded — tab bar still renders
    // the dropdown + a "pick a file" hint, body shows an empty-state prompt.
    if (!docs) {
        ui::theme::PopEditorStyle();
        ImGui::End();
        return;
    }

    const bool docs_empty = docs->empty();
    if (!docs_empty) {
        if (*active_doc < 0) *active_doc = 0;
        if (*active_doc >= (int)docs->size()) *active_doc = (int)docs->size() - 1;
    } else {
        *active_doc = -1;
    }

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    RenderTabBar(s, *docs, active_doc, out_close_indices,
                 directory_files, directory_label,
                 out_pending_paths, out_clear_directory);
    if (s.ui_font) ImGui::PopFont();

    // RenderTabBar may have mutated *active_doc; re-clamp before use.
    if (!docs->empty()) {
        if (*active_doc < 0) *active_doc = 0;
        if (*active_doc >= (int)docs->size()) *active_doc = (int)docs->size() - 1;
    }

    if (docs->empty()) {
        // Folder loaded, nothing open — render a centered prompt below the tab bar.
        ImGui::Separator();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        if (s.ui_font) ImGui::PushFont(s.ui_font);
        const char* line1 = "Choose a file to open";
        const char* line2 = (directory_label && !directory_label->empty())
            ? "Click the folder icon above to pick from the directory."
            : "Drag a file here or click the folder icon above.";
        ImVec2 sz1 = ImGui::CalcTextSize(line1);
        ImVec2 sz2 = ImGui::CalcTextSize(line2);
        if (s.ui_font) ImGui::PopFont();

        const float gap = 8.0f;
        float block_h = sz1.y + gap + sz2.y;
        float top = origin.y + (avail.y - block_h) * 0.5f;
        if (top < origin.y + 20.0f) top = origin.y + 20.0f;
        float cx = origin.x + avail.x * 0.5f;

        if (s.ui_font) ImGui::PushFont(s.ui_font);
        ImGui::SetCursorScreenPos(ImVec2(cx - sz1.x * 0.5f, top));
        ImGui::PushStyleColor(ImGuiCol_Text, pal.start_title_text);
        ImGui::TextUnformatted(line1);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(cx - sz2.x * 0.5f, top + sz1.y + gap));
        ImGui::TextDisabled("%s", line2);
        if (s.ui_font) ImGui::PopFont();

        ui::theme::PopEditorStyle();
        ImGui::End();
        return;
    }

    OpenDocument& od = (*docs)[*active_doc];
    if (!od.core) {
        // Should never happen — push_back is gated on a successful ctor —
        // but a partially-constructed entry would null-deref otherwise.
        ui::theme::PopEditorStyle();
        ImGui::End();
        return;
    }
    HexEditorCore&     core_ref = *od.core;
    ui::DocumentState& doc      = od.doc_state;

    // Reset each frame; toolbar/grid re-set via IsItemActive.
    doc.focus_field = (doc.selected_byte >= 0) ? ui::GuiState::FOCUS_BYTE
                                               : ui::GuiState::FOCUS_NONE;

    // Latches until the user resolves it — auto-clearing would rebase the
    // baseline without confirmation. Sticky status so it can't be missed.
    if (!doc.externally_modified && core_ref.HasExternalModification()) {
        doc.externally_modified = true;
        s.SetStatus("File changed on disk", ui::GuiState::STATUS_WARN, true);
    }

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    ui::RenderToolbar(s, doc, pal, core_ref);
    if (s.ui_font) ImGui::PopFont();
    ImGui::Separator();

    if (s.mono_font) ImGui::PushFont(s.mono_font);
    // ComputeHexLayout's CalcTextSize assumes window scale == 1.0 (it multiplies internally).
    // Restore 1.0 for the layout calc, then restore chrome_scale for the status-bar reservation.
    ImGui::SetWindowFontScale(1.0f);
    ui::HexLayout layout =
        ui::ComputeHexLayout(ImGui::GetContentRegionAvail().x, s.font_scale);
    ImGui::SetWindowFontScale(s.chrome_scale);

    // Zero WindowPadding so header and body share window.Pos.x — otherwise byte columns
    // drift out from under the header labels. SetWindowFontScale must sit between BeginChild
    // and the renderer (per-window).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
    ImGui::BeginChild("##hexheader",
                      ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::SetWindowFontScale(s.font_scale);
    ImGui::PopStyleVar();
    ui::RenderHexHeader(pal, layout);
    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
    ImGui::BeginChild("##hexview",
                      ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      false,
                      ImGuiWindowFlags_None);
    ImGui::SetWindowFontScale(s.font_scale);
    ImGui::PopStyleVar();
    // For HandleShortcuts: PgUp/PgDn moves exactly one screenful.
    const float hex_body_height = ImGui::GetWindowHeight();
    ui::RenderHexGrid(s, doc, pal, core_ref, layout);
    ImGui::EndChild();
    if (s.mono_font) ImGui::PopFont();

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    ui::RenderStatusBar(s, doc, pal, core_ref);
    if (s.ui_font) ImGui::PopFont();

    if (s.show_settings) {
        ImGui::OpenPopup("Settings##settings");
        s.show_settings = false;
    }
    ui::RenderSettingsPopup(s);

    if (doc.conflict_modal_open) {
        ImGui::OpenPopup("File changed on disk##conflict");
        doc.conflict_modal_open = false;
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
                doc.externally_modified  = false;
                doc.pending_edit_offset  = -1;
                doc.pending_undo         = false;
                // File may have shrunk — clamp positions past EOF.
                const int64_t new_size = core_ref.GetFileSize();
                if (new_size <= 0) {
                    doc.caret_byte = -1;
                    doc.last_hit   = -1;
                } else {
                    if (doc.caret_byte >= new_size) doc.caret_byte = new_size - 1;
                    if (doc.last_hit   >= new_size) doc.last_hit   = -1;
                }
                doc.selected_byte = -1;
                s.SetStatus("Reloaded from disk", ui::GuiState::STATUS_OK);
            } else {
                s.SetStatus("Reload failed", ui::GuiState::STATUS_ERROR);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Keep my edits", ImVec2(140, 0))) {
            // Rebaseline up-front — otherwise the next-frame HasExternalModification re-latches
            // the warning (Cmd+Z path sets no pending_edit, so it'd leave baseline stale).
            doc.externally_modified = false;
            core_ref.Rebaseline();
            if (doc.pending_edit_offset >= 0) {
                auto res = core_ref.EditByte(doc.pending_edit_offset,
                                             doc.pending_edit_value);
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
                doc.pending_edit_offset = -1;
            } else if (doc.pending_undo) {
                // Replay the Cmd+Z that opened this modal — externally_modified is now false
                // so DoUndo runs through, not back into this dialog.
                doc.pending_undo = false;
                ui::DoUndo(s, doc, core_ref);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            doc.pending_edit_offset = -1;
            doc.pending_undo        = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    HandleShortcuts(s, doc, core_ref, layout, hex_body_height);
    HandleTabShortcuts(*docs, active_doc, out_close_indices);

    ui::theme::PopEditorStyle();
    ImGui::End();
}
