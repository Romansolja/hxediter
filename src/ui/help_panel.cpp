#include "ui/help_panel.h"
#include "ui/layout.h"

#include "imgui.h"

namespace ui {

void RenderHelpPanel(GuiState& s, const theme::Palette& pal, float visibility) {
    // Combined font zoom + HiDPI multiplier so pixel constants stay
    // proportional on 4K.
    const float scale = s.font_scale * s.content_scale;

    // Chord prefix reads "Cmd" on macOS, matching the README and Apple's
    // HIG; ShortcutHeld() also accepts Ctrl for muscle-memory-from-Windows
    // and Karabiner-style key remappers, but the canonical shortcut is Cmd.
    const char* lines[] = {
        "Quick reference",
        "",
        "  Click byte           Edit hex value in place",
        "  Arrow keys           Move caret in byte grid",
        "  Home / End           Row start / end",
        "  F2                   Start editing byte under caret",
        "  Enter                Commit edit",
        "  Esc                  Cancel edit",
        "  PgUp / PgDn          Move caret one screen",
        "  Cmd+Z                Undo last edit",
        "  Cmd+= / Cmd+-        Scale font up / down",
        "  Cmd+scroll           Scale font (mouse / trackpad)",
        "  Cmd+0                Reset font scale",
        "  Cmd+Shift+P          Cycle color palette",
        "  Cmd+Tab / Shift      Next / previous tab",
        "  Cmd+W                Close current tab",
        "  Cmd+1 .. Cmd+9       Jump to tab N",
        "  F1                   Toggle this panel",
        "  Goto field           Jump to a hex offset",
        "  Search field         Find a hex byte sequence",
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0]));

    const float pad_x   = layout::kHelpPanelPadX * scale;
    const float pad_y   = layout::kHelpPanelPadY * scale;
    float       panel_w = layout::kHelpPanelWidth * scale;
    float line_h = ImGui::GetTextLineHeightWithSpacing();
    float shown_h = (pad_y * 2 + line_h * (float)n) * visibility;

    if (shown_h < 20.0f) return;

    // Center in the hex view's visible rect (screen-space) so the panel
    // stays put as rows fill the page or the user scrolls.
    const ImVec2 win_pos  = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    const float  margin   = 16.0f * scale;
    // Shrink to fit instead of vanishing when HiDPI + 100% font scale
    // pushes the natural panel past the hex view child's bounds. The line
    // loop below already drops rows that fall past the clamped bottom.
    if (panel_w > win_size.x - margin) panel_w = win_size.x - margin;
    if (shown_h > win_size.y - margin) shown_h = win_size.y - margin;
    if (panel_w < layout::kHelpPanelMinW * scale ||
        shown_h < layout::kHelpPanelMinH * scale) return;

    const ImVec2 saved_cursor = ImGui::GetCursorScreenPos();

    ImVec2 p0(win_pos.x + (win_size.x - panel_w) * 0.5f,
              win_pos.y + (win_size.y - shown_h) * 0.5f);
    ImVec2 p1(p0.x + panel_w, p0.y + shown_h);

    ImDrawList* dl = ImGui::GetWindowDrawList();

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

    // Absorb clicks anywhere on the panel so they neither dismiss the
    // overlay (HandleShortcuts' !IsAnyItemHovered() rule) nor fall through
    // to the byte selectables underneath. AllowOverlap so the close button
    // drawn next still wins in its top-right corner.
    ImGui::SetCursorScreenPos(p0);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##help_blocker", ImVec2(panel_w, shown_h));

    const float x_sz = layout::kHelpCloseSize * scale;
    ImVec2 x_pos(p1.x - x_sz - 6.0f * scale, p0.y + 6.0f * scale);
    ImGui::SetCursorScreenPos(x_pos);

    ImVec4 hover_c = pal.help_close_hover;  hover_c.w *= visibility;
    ImVec4 act_c   = pal.help_close_active; act_c.w   *= visibility;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover_c);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act_c);
    if (ImGui::Button("##help_close", ImVec2(x_sz, x_sz))) {
        s.show_help = false;
    }
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
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, body_c);
            ImGui::TextUnformatted(lines[i]);
            ImGui::PopStyleColor();
        }
    }

    // Restore cursor so subsequent widgets don't lay out from inside the panel.
    ImGui::SetCursorScreenPos(saved_cursor);
}

} // namespace ui
