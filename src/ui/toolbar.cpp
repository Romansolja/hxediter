#include "ui/toolbar.h"
#include "ui/actions.h"
#include "ui/layout.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

namespace ui {

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

    /* Lift every following Text() onto the same baseline as buttons/fields. */
    ImGui::AlignTextToFramePadding();

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

    ImGui::SameLine(0.0f, group_gap);
    /* Glyph is always "?"; when the quick reference is up, paint the
     * resting state with the hover tint so the button reads as "pressed"
     * without a distinct label. Dismissing the panel returns it to the
     * normal secondary style. */
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

    /* Gear/Settings button pinned to the far right. Drawn manually via
     * InvisibleButton + DrawList so the glyph is pixel-centered — the
     * default ImGui::Button layout uses the font's advance metrics, which
     * leaves FontAwesome glyphs slightly off-center in a square button.
     * Teal accent separates it visually from the editor-action buttons. */
    const bool  have_icon = (s.icon_font_small != nullptr);
    const float size      = ImGui::GetFrameHeight();
    const float btn_w     = have_icon ? size : 80.0f;
    const float pad       = 8.0f;
    const float desired_x = ImGui::GetWindowContentRegionMax().x - btn_w - pad;
    const float current_x = ImGui::GetCursorPosX();
    if (desired_x > current_x) ImGui::SameLine(desired_x);
    else                       ImGui::SameLine();

    if (have_icon) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        /* Sample popup state before the click is processed — if the
         * non-modal popup is open, BeginPopup's click-outside handling
         * will close it this frame, so swallow the reopen to keep the
         * gear feeling like a proper toggle instead of a flicker. */
        const bool popup_open = ImGui::IsPopupOpen("Settings##settings");
        const bool clicked = ImGui::InvisibleButton("##settings", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();
        if (clicked && !popup_open) s.show_settings = true;

        /* JetBrains-IDE feel: no background at rest — the gear floats on
         * the toolbar like an icon, not a button. A subtle white-alpha
         * tint appears on hover, and a slightly stronger one while held.
         * Pushed zero-alpha NavHighlight defensively so nav focus after
         * a click doesn't paint a ring on mouse-back-over. */
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
        /* Slightly brighter on hover so the glyph reads as "responsive". */
        const ImU32 glyph = hovered ? IM_COL32(215, 220, 228, 255)
                                    : IM_COL32(168, 174, 182, 220);
        dl->AddText(tp, glyph, ICON_FA_GEAR);
        ImGui::PopFont();

        if (hovered) {
            /* Mirror the Settings popup's frame: rounded corners and the
             * mono font. SetTooltip would render with the default frame
             * style, which visually disagrees with the panel it launches. */
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
