#include "ui/start_screen.h"
#include "ui/layout.h"
#include "platform/file_dialogs.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <cstdio>
#include <utility>

namespace ui {

void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       int drag_over_state,
                       std::vector<std::string>* out_pending_directories,
                       std::vector<std::string>* out_pending_triage_root) {
    ImVec2 avail  = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    /* Fade the drop-zone overlay in/out at the help-panel animation speed. */
    {
        float target = (drag_over_state != 0) ? 1.0f : 0.0f;
        float t = ImGui::GetIO().DeltaTime * layout::kHelpAnimSpeed;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        s.drag_overlay_anim += (target - s.drag_overlay_anim) * t;
    }

    ImU32 top_col    = ImGui::GetColorU32(pal.start_bg_top);
    ImU32 bottom_col = ImGui::GetColorU32(pal.start_bg_bottom);
    dl->AddRectFilledMultiColor(
        origin,
        ImVec2(origin.x + avail.x, origin.y + avail.y),
        top_col, top_col, bottom_col, bottom_col);

    const char* title_str   = "HxEditer";
    const char* icon_str    = ICON_FA_FILE;
    const char* drop_str    = "Drag and drop a file or folder here";
    const char* button_str  = "Select File";
    const char* button_folder_str = "Open Folder...";
    const char* button2_str = "Triage Folder...";
    const float gap_between_buttons = 12.0f;

    ImVec2 title_sz, icon_sz, drop_sz, button_label_sz, button_folder_label_sz, button2_label_sz;

    if (s.title_font) ImGui::PushFont(s.title_font);
    title_sz = ImGui::CalcTextSize(title_str);
    if (s.title_font) ImGui::PopFont();

    if (s.icon_font) {
        ImGui::PushFont(s.icon_font);
        icon_sz = ImGui::CalcTextSize(icon_str);
        ImGui::PopFont();
    } else {
        icon_sz = ImVec2(96.0f, 96.0f);
    }

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    drop_sz                 = ImGui::CalcTextSize(drop_str);
    button_label_sz         = ImGui::CalcTextSize(button_str);
    button_folder_label_sz  = ImGui::CalcTextSize(button_folder_str);
    button2_label_sz        = ImGui::CalcTextSize(button2_str);
    if (s.ui_font) ImGui::PopFont();

    ImGuiStyle& style = ImGui::GetStyle();
    /* All three buttons share the widest label width so they line up
     * vertically. Heights are equal because text height is identical. */
    float button_label_w = button_label_sz.x;
    if (button_folder_label_sz.x > button_label_w) button_label_w = button_folder_label_sz.x;
    if (button2_label_sz.x       > button_label_w) button_label_w = button2_label_sz.x;
    ImVec2 button_sz(button_label_w + style.FramePadding.x * 2.0f + 40.0f,
                     button_label_sz.y + style.FramePadding.y * 2.0f + 12.0f);

    const float gap_icon_to_title  = layout::kStartIconToTitle;
    const float gap_title_to_drop  = layout::kStartTitleToDrop;
    const float gap_drop_to_button = layout::kStartDropToButton;
    const float gap_button_to_err  = layout::kStartButtonToErr;

    float col_h = icon_sz.y + gap_icon_to_title
                + title_sz.y + gap_title_to_drop
                + drop_sz.y  + gap_drop_to_button
                + button_sz.y
                + gap_between_buttons + button_sz.y
                + gap_between_buttons + button_sz.y;
    if (load_error && *load_error)
        col_h += gap_button_to_err + ImGui::GetTextLineHeight();

    float col_top = origin.y + (avail.y - col_h) * 0.5f;
    if (col_top < origin.y + 20.0f) col_top = origin.y + 20.0f;
    float col_cx  = origin.x + avail.x * 0.5f;

    if (s.icon_font) {
        ImGui::PushFont(s.icon_font);
        ImGui::SetCursorScreenPos(ImVec2(col_cx - icon_sz.x * 0.5f, col_top));
        ImGui::PushStyleColor(ImGuiCol_Text, pal.start_icon);
        ImGui::TextUnformatted(icon_str);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    } else {
        ImVec2 p0(col_cx - 48.0f, col_top);
        ImVec2 p1(col_cx + 48.0f, col_top + 96.0f);
        ImVec4 fill = pal.start_icon; fill.w = 0.25f;
        ImVec4 line = pal.start_icon; line.w = 1.00f;
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(fill), 12.0f);
        dl->AddRect      (p0, p1, ImGui::GetColorU32(line), 12.0f, 0, 2.0f);
    }

    float y = col_top + icon_sz.y + gap_icon_to_title;

    if (s.title_font) ImGui::PushFont(s.title_font);
    ImGui::SetCursorScreenPos(ImVec2(col_cx - title_sz.x * 0.5f, y));
    ImGui::PushStyleColor(ImGuiCol_Text, pal.start_title_text);
    ImGui::TextUnformatted(title_str);
    ImGui::PopStyleColor();
    if (s.title_font) ImGui::PopFont();
    y += title_sz.y + gap_title_to_drop;

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    ImGui::SetCursorScreenPos(ImVec2(col_cx - drop_sz.x * 0.5f, y));
    ImGui::TextDisabled("%s", drop_str);
    y += drop_sz.y + gap_drop_to_button;

    ImGui::SetCursorScreenPos(ImVec2(col_cx - button_sz.x * 0.5f, y));
    ImGui::PushStyleColor(ImGuiCol_Button,        pal.btn_primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.btn_primary_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pal.btn_primary_active);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(20.0f, 10.0f));

    if (ImGui::Button(button_str, button_sz)) {
        if (auto picked = platform::OpenFileDialog(
                s.native_window_handle, "Choose a file to open")) {
            if (out_pending_paths)
                out_pending_paths->push_back(std::move(*picked));
        }
    }

    y += button_sz.y + gap_between_buttons;
    /* Second button: "Open Folder..." picks a directory and surfaces its
     * files in the tab-bar dropdown — same destination as a folder
     * drag-drop or a folder passed on the CLI. Distinct from triage,
     * which moves files into _junk/_review/_duplicates buckets. */
    ImGui::SetCursorScreenPos(ImVec2(col_cx - button_sz.x * 0.5f, y));
    if (ImGui::Button(button_folder_str, button_sz)) {
        if (auto picked = platform::PickFolderDialog(
                s.native_window_handle, "Choose a folder to open")) {
            if (out_pending_directories)
                out_pending_directories->push_back(std::move(*picked));
        }
    }

    y += button_sz.y + gap_between_buttons;
    /* Third button: "Triage Folder..." opens the OS-native folder picker. */
    ImGui::SetCursorScreenPos(ImVec2(col_cx - button_sz.x * 0.5f, y));
    if (ImGui::Button(button2_str, button_sz)) {
        if (auto picked = platform::PickFolderDialog(
                s.native_window_handle, "Choose a folder to triage")) {
            if (out_pending_triage_root)
                out_pending_triage_root->push_back(std::move(*picked));
        }
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    y += button_sz.y;

    if (load_error && *load_error) {
        y += gap_button_to_err;
        ImVec2 err_sz = ImGui::CalcTextSize(load_error);
        ImGui::SetCursorScreenPos(ImVec2(col_cx - err_sz.x * 0.5f, y));
        ImGui::PushStyleColor(ImGuiCol_Text, pal.start_error_text);
        ImGui::TextUnformatted(load_error);
        ImGui::PopStyleColor();
    }

    if (s.ui_font) ImGui::PopFont();

    char metric[32];
    if (s.startup_measured)
        std::snprintf(metric, sizeof(metric), "Startup: %.0f ms", s.startup_duration_ms);
    else
        std::snprintf(metric, sizeof(metric), "Startup: \xE2\x80\xA6");

    if (s.ui_font) ImGui::PushFont(s.ui_font);
    ImVec2 metric_sz = ImGui::CalcTextSize(metric);
    const float pad = 12.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - metric_sz.x - pad,
                                     origin.y + avail.y - metric_sz.y - pad));
    ImGui::TextDisabled("%s", metric);

    /* Top-right read-only toggle; main.cpp consults this via
     * ReadonlyDefault() right after constructing a HexEditorCore. */
    const float box_h = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - box_h - pad,
                                     origin.y + pad));
    ImGui::Checkbox("##readonly_default", &s.readonly_default);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open files as read-only");
    }
    if (s.ui_font) ImGui::PopFont();

    if (s.drag_overlay_anim > 0.001f) {
        const bool invalid = (drag_over_state == 2);
        const float anim   = s.drag_overlay_anim;
        const ImVec4 tint  = invalid ? pal.status_err_bg : pal.btn_primary;

        ImVec4 fill = tint;
        fill.w = (invalid ? 0.28f : 0.22f) * anim;
        dl->AddRectFilled(origin,
                          ImVec2(origin.x + avail.x, origin.y + avail.y),
                          ImGui::GetColorU32(fill));
    }
}

} /* namespace ui */
