#include "ui/help_panel.h"
#include "ui/layout.h"

#include "imgui.h"

namespace ui {

void RenderHelpPanel(GuiState& s, const theme::Palette& pal, float visibility) {
    const float scale = s.font_scale;

    float remaining = ImGui::GetContentRegionAvail().y;
    if (remaining < 90.0f * scale) return;

    ImGui::Dummy(ImVec2(0, 12.0f * scale));

    const float pad_x   = layout::kHelpPanelPadX * scale;
    const float pad_y   = layout::kHelpPanelPadY * scale;
    const float panel_w = layout::kHelpPanelWidth * scale;
    float avail_w = ImGui::GetContentRegionAvail().x;
    float indent  = (avail_w > panel_w) ? (avail_w - panel_w) * 0.5f : 0.0f;
    if (indent > 0.0f) ImGui::Indent(indent);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const char* lines[] = {
        "Quick reference",
        "",
        "  Click byte           Edit hex value in place",
        "  Arrow keys           Move caret in byte grid",
        "  Home / End           Row start / end",
        "  F2                   Start editing byte under caret",
        "  Enter                Commit edit",
        "  Esc                  Cancel edit",
        "  PgUp / PgDn          Previous / next page",
        "  Ctrl+Z               Undo last edit",
        "  Ctrl+= / Ctrl+-      Scale font up / down",
        "  Ctrl+0               Reset font scale",
        "  Ctrl+Shift+P         Cycle color palette",
        "  F1                   Toggle this panel",
        "  Goto field           Jump to a hex offset",
        "  Search field         Find a hex byte sequence",
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    float line_h = ImGui::GetTextLineHeightWithSpacing();
    float panel_h = pad_y * 2 + line_h * (float)n;
    float shown_h = panel_h * visibility;

    if (shown_h > remaining - 8.0f || shown_h < 20.0f) {
        if (indent > 0.0f) ImGui::Unindent(indent);
        return;
    }

    ImVec2 p1(p0.x + panel_w, p0.y + shown_h);

    dl->AddRectFilled(ImVec2(p0.x + 4.0f * scale, p0.y + 5.0f * scale),
                      ImVec2(p1.x + 6.0f * scale, p1.y + 8.0f * scale),
                      ImGui::GetColorU32(ImVec4(0.00f, 0.00f, 0.00f, 0.20f * visibility)),
                      8.0f);
    dl->AddRectFilled(ImVec2(p0.x + 2.0f * scale, p0.y + 3.0f * scale),
                      ImVec2(p1.x + 3.0f * scale, p1.y + 4.0f * scale),
                      ImGui::GetColorU32(ImVec4(0.00f, 0.00f, 0.00f, 0.12f * visibility)),
                      8.0f);

    ImVec4 bg = pal.help_panel_bg;     bg.w *= visibility;
    ImVec4 bd = pal.help_panel_border; bd.w *= visibility;
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(bg),
                      layout::kHelpPanelRounding);
    dl->AddRect      (p0, p1, ImGui::GetColorU32(bd),
                      layout::kHelpPanelRounding, 0, 1.5f);

    const float x_sz = layout::kHelpCloseSize * scale;
    ImVec2 x_pos(p1.x - x_sz - 6.0f * scale, p0.y + 6.0f * scale);
    ImGui::SetCursorScreenPos(x_pos);

    ImVec4 hover_c = pal.help_close_hover;  hover_c.w *= visibility;
    ImVec4 act_c   = pal.help_close_active; act_c.w   *= visibility;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover_c);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act_c);
    if (visibility <= 0.0f) ImGui::BeginDisabled();
    if (ImGui::Button("##help_close", ImVec2(x_sz, x_sz))) {
        s.show_help = false;
    }
    if (visibility <= 0.0f) ImGui::EndDisabled();
    ImGui::PopStyleColor(3);

    ImVec4 glyph = pal.help_close_glyph; glyph.w *= visibility;
    ImU32 x_col = ImGui::GetColorU32(glyph);
    float pad = 5.0f * scale;
    dl->AddLine(ImVec2(x_pos.x + pad,        x_pos.y + pad),
                ImVec2(x_pos.x + x_sz - pad, x_pos.y + x_sz - pad), x_col, 1.5f);
    dl->AddLine(ImVec2(x_pos.x + x_sz - pad, x_pos.y + pad),
                ImVec2(x_pos.x + pad,        x_pos.y + x_sz - pad), x_col, 1.5f);

    ImVec4 title_c = pal.help_title_text; title_c.w *= visibility;
    ImVec4 body_c  = pal.help_body_text;  body_c.w  *= visibility;

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + pad_y));
    for (int i = 0; i < n; ++i) {
        float line_y = p0.y + pad_y + line_h * (float)i;
        if (line_y + line_h > p1.y - pad_y * 0.25f) break;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + pad_x, line_y));
        if (i == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, title_c);
            ImGui::TextUnformatted(lines[i]);
            ImGui::PopStyleColor();
        } else if (lines[i][0] == '\0') {
            /* spacer row */
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, body_c);
            ImGui::TextUnformatted(lines[i]);
            ImGui::PopStyleColor();
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 4.0f * scale));
    /* Register the manually-moved cursor with ImGui's layout bookkeeping;
     * without this the next widget can trip a cursor assertion and the
     * panel won't contribute to content size for scrolling. */
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    if (indent > 0.0f) ImGui::Unindent(indent);
}

} /* namespace ui */
