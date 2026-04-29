#include "ui/toolbar.h"
#include "ui/actions.h"
#include "ui/layout.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <algorithm>

namespace ui {

void RenderToolbar(GuiState& s, DocumentState& doc,
                   const theme::Palette& pal, HexEditorCore& core) {
    const float group_gap = layout::kToolbarGroupGap;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fp = style.FramePadding.x * 2.0f;
    const float is = style.ItemSpacing.x;

    auto PushPrimary = [&]() {
        ImGui::PushStyleColor(ImGuiCol_Button,        pal.btn_primary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.btn_primary_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pal.btn_primary_active);
    };
    auto PushSecondary = [&]() {
        ImGui::PushStyleColor(ImGuiCol_Button,        pal.btn_secondary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.btn_secondary_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pal.btn_secondary_active);
    };
    auto PopBtn = []() { ImGui::PopStyleColor(3); };

    ImGui::AlignTextToFramePadding();

    /* Responsive pass: measure every element, then decide field widths and
     * which low-priority elements to drop. Keeps the toolbar from running
     * under the right-anchored settings gear. */
    auto textw = [](const char* t) { return ImGui::CalcTextSize(t).x; };

    const float w_jump_label = textw("JUMP");
    const float w_goto_label = textw("Goto:");
    const float w_go         = textw("Go") + fp;
    const float w_search_lbl = textw("SEARCH/EDIT");
    const float w_find       = textw("Find") + fp;
    const float w_undo       = textw("Undo") + fp;
    const float w_help       = textw("?") + fp;
    const float w_gear       = ImGui::GetFrameHeight();
    const float gear_pad     = 8.0f;

    const float goto_nominal   = layout::kGotoFieldWidth;
    const float search_nominal = layout::kSearchFieldWidth;
    const float goto_min       = 60.0f;
    const float search_min     = 80.0f;

    bool show_undo         = true;
    bool show_find         = true;
    bool show_help         = true;
    bool show_jump_label   = true;
    bool show_search_label = true;
    bool show_search_group = true;

    float goto_w   = goto_nominal;
    float search_w = search_nominal;

    auto compute_needed = [&]() {
        float w = (show_jump_label ? (w_jump_label + is) : 0.0f)
                + w_goto_label + is + goto_w + is + w_go;
        if (show_search_group) {
            w += group_gap + (show_search_label ? (w_search_lbl + is) : 0.0f)
               + search_w;
            if (show_find) w += is + w_find;
            if (show_undo) w += is + w_undo;
        }
        if (show_help) w += group_gap + w_help;
        w += group_gap + w_gear + gear_pad;
        return w;
    };

    const float avail = ImGui::GetWindowContentRegionMax().x - ImGui::GetCursorPosX();

    if (compute_needed() > avail) {
        /* Shrink both fields proportionally, capped at their minimums. */
        float overflow   = compute_needed() - avail;
        float field_sum  = goto_w + search_w;
        float goto_share = goto_w / field_sum;
        goto_w   = std::max(goto_min,   goto_w   - overflow * goto_share);
        search_w = std::max(search_min, search_w - overflow * (1.0f - goto_share));
    }
    /* Drop low-priority elements one at a time until it fits. */
    if (compute_needed() > avail) show_undo         = false;
    if (compute_needed() > avail) show_find         = false;
    if (compute_needed() > avail) show_help         = false;
    if (compute_needed() > avail) show_search_label = false;
    if (compute_needed() > avail) show_jump_label   = false;
    if (compute_needed() > avail) show_search_group = false;

    if (show_jump_label) {
        ImGui::TextDisabled("JUMP");
        ImGui::SameLine();
    }
    ImGui::TextUnformatted("Goto:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(goto_w);
    if (ImGui::InputText("##goto", doc.goto_buf, sizeof(doc.goto_buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        DoGoto(s, doc, core);
    }
    if (ImGui::IsItemActive()) doc.focus_field = GuiState::FOCUS_GOTO;
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Go")) DoGoto(s, doc, core);
    PopBtn();

    if (show_search_group) {
        ImGui::SameLine(0.0f, group_gap);

        if (show_search_label) {
            ImGui::TextDisabled("SEARCH/EDIT");
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(search_w);
        if (ImGui::InputText("##search", doc.search_buf, sizeof(doc.search_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            DoSearch(s, doc, core);
        }
        /* Pattern changed — restart from caret/0 instead of resuming past
         * the previous match for a different pattern. */
        if (ImGui::IsItemEdited()) doc.last_hit = -1;
        if (ImGui::IsItemActive()) doc.focus_field = GuiState::FOCUS_SEARCH;
        if (show_find) {
            ImGui::SameLine();
            PushPrimary();
            if (ImGui::Button("Find")) DoSearch(s, doc, core);
            PopBtn();
        }
        if (show_undo) {
            ImGui::SameLine();
            PushSecondary();
            if (ImGui::Button("Undo")) DoUndo(s, doc, core);
            PopBtn();
        }
    }

    if (show_help) {
        ImGui::SameLine(0.0f, group_gap);
        /* While the quick reference is up, paint the resting state with
         * the hover tint so the button reads as "pressed". */
        if (s.show_help) {
            ImGui::PushStyleColor(ImGuiCol_Button,        pal.btn_secondary_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.btn_secondary_active);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pal.btn_secondary_active);
        } else {
            PushSecondary();
        }
        if (ImGui::Button("?##quickref")) {
            s.show_help = !s.show_help;
        }
        PopBtn();
        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
            ImGui::BeginTooltip();
            if (s.mono_font) ImGui::PushFont(s.mono_font);
            ImGui::TextUnformatted("Toggle quick reference (F1)");
            if (s.mono_font) ImGui::PopFont();
            ImGui::EndTooltip();
            ImGui::PopStyleVar(2);
        }
    }

    /* InvisibleButton + DrawList so the gear glyph is pixel-centered;
     * ImGui::Button uses font advance metrics and leaves FA glyphs
     * slightly off-center in a square button. */
    const bool  have_icon = (s.icon_font_small != nullptr);
    const float size      = ImGui::GetFrameHeight();
    const float btn_w     = have_icon ? size : 80.0f * s.content_scale;
    const float pad       = 8.0f * s.content_scale;
    const float desired_x = ImGui::GetWindowContentRegionMax().x - btn_w - pad;
    const float current_x = ImGui::GetCursorPosX();
    if (desired_x > current_x) ImGui::SameLine(desired_x);
    else                       ImGui::SameLine();

    if (have_icon) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        /* Sample before the click: BeginPopup's click-outside closes this
         * frame, so a click on the gear while open should toggle off, not
         * re-open. */
        const bool popup_open = ImGui::IsPopupOpen("Settings##settings");
        const bool clicked = ImGui::InvisibleButton("##settings", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();
        if (clicked && !popup_open) s.show_settings = true;

        ImU32 bg = 0;
        if      (active)  bg = IM_COL32(255, 255, 255, 34);
        else if (hovered) bg = IM_COL32(255, 255, 255, 20);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (bg != 0) {
            dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), bg, 4.0f);
        }

        ImGui::PushFont(s.icon_font_small);
        const ImVec2 tsz = ImGui::CalcTextSize(ICON_FA_GEAR);
        const ImVec2 tp(pos.x + (size - tsz.x) * 0.5f,
                        pos.y + (size - tsz.y) * 0.5f);
        const ImU32 glyph = hovered ? IM_COL32(215, 220, 228, 255)
                                    : IM_COL32(168, 174, 182, 220);
        dl->AddText(tp, glyph, ICON_FA_GEAR);
        ImGui::PopFont();

        if (hovered) {
            /* Mirror the Settings popup frame so the tooltip agrees
             * visually with the panel it launches. */
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
            ImGui::BeginTooltip();
            if (s.mono_font) ImGui::PushFont(s.mono_font);
            ImGui::TextUnformatted("Settings");
            if (s.mono_font) ImGui::PopFont();
            ImGui::EndTooltip();
            ImGui::PopStyleVar(2);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 44,  98, 122, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32( 62, 132, 158, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32( 32,  72,  94, 255));
        const bool popup_open = ImGui::IsPopupOpen("Settings##settings");
        if (ImGui::Button("Settings##settings") && !popup_open) {
            s.show_settings = true;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
            ImGui::BeginTooltip();
            if (s.mono_font) ImGui::PushFont(s.mono_font);
            ImGui::TextUnformatted("Settings");
            if (s.mono_font) ImGui::PopFont();
            ImGui::EndTooltip();
            ImGui::PopStyleVar(2);
        }
    }
}

} /* namespace ui */
