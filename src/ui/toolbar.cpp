/* toolbar.cpp — Editor toolbar with NAV / JUMP / SEARCH-EDIT groups. */

#include "ui/toolbar.h"
#include "ui/actions.h"
#include "ui/layout.h"

#include "imgui.h"

namespace ui {

/* Three task groups: NAV (paging), JUMP (offset entry), SEARCH/EDIT
 * (find + undo). Each group has one primary action styled brighter,
 * with extra horizontal space between groups so the eye can parse
 * the toolbar as three things instead of nine. */
void RenderToolbar(GuiState& s, const theme::Palette& pal, HexEditorCore& core) {
    const float group_gap = layout::kToolbarGroupGap;

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

    /* Aligning text to frame padding once at the start of the line
     * lifts every following Text() so labels sit on the same baseline
     * as buttons and input fields. */
    ImGui::AlignTextToFramePadding();

    /* ---------------- Navigation ---------------- */
    ImGui::TextDisabled("NAV");
    ImGui::SameLine();
    PushSecondary();
    if (ImGui::Button("<< Prev")) {
        if (core.PagePrev()) s.MarkInteracted();
        else                 s.SetStatus("At start of file", GuiState::STATUS_WARN);
    }
    PopBtn();
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Next >>")) {
        if (core.PageNext()) s.MarkInteracted();
        else                 s.SetStatus("At end of file", GuiState::STATUS_WARN);
    }
    PopBtn();

    ImGui::SameLine(0.0f, group_gap);

    /* ---------------- Jump ---------------- */
    ImGui::TextDisabled("JUMP");
    ImGui::SameLine();
    ImGui::TextUnformatted("Goto:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(layout::kGotoFieldWidth);
    if (ImGui::InputText("##goto", s.goto_buf, sizeof(s.goto_buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        DoGoto(s, core);
    }
    if (ImGui::IsItemActive()) s.focus_field = GuiState::FOCUS_GOTO;
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Go")) DoGoto(s, core);
    PopBtn();

    ImGui::SameLine(0.0f, group_gap);

    /* ---------------- Search / Edit ---------------- */
    ImGui::TextDisabled("SEARCH/EDIT");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(layout::kSearchFieldWidth);
    if (ImGui::InputText("##search", s.search_buf, sizeof(s.search_buf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        DoSearch(s, core);
    }
    if (ImGui::IsItemActive()) s.focus_field = GuiState::FOCUS_SEARCH;
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Find")) DoSearch(s, core);
    PopBtn();
    ImGui::SameLine();
    PushSecondary();
    if (ImGui::Button("Undo")) DoUndo(s, core);
    PopBtn();

    /* Help toggle, right-aligned-ish with extra gap. */
    ImGui::SameLine(0.0f, group_gap);
    PushSecondary();
    if (ImGui::Button(s.show_help ? "Hide ?" : "?")) {
        s.show_help = !s.show_help;
    }
    PopBtn();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle quick reference (F1)");
}

} /* namespace ui */
