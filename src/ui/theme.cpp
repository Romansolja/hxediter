#include "ui/theme.h"
#include "ui/layout.h"

namespace ui::theme {

const Palette kDefault = {
    /* text             */ ImVec4(0.94f, 0.95f, 0.97f, 1.00f),
    /* text_disabled    */ ImVec4(0.62f, 0.66f, 0.74f, 1.00f),
    /* frame_bg         */ ImVec4(0.13f, 0.15f, 0.19f, 1.00f),
    /* frame_bg_hovered */ ImVec4(0.20f, 0.24f, 0.32f, 1.00f),
    /* frame_bg_active  */ ImVec4(0.26f, 0.34f, 0.48f, 1.00f),
    /* border           */ ImVec4(0.36f, 0.42f, 0.54f, 1.00f),
    /* nav_cursor       */ ImVec4(0.40f, 0.70f, 1.00f, 1.00f),

    /* byte_zero        */ ImVec4(0.45f, 0.45f, 0.45f, 1.00f),
    /* byte_printable   */ ImVec4(0.95f, 0.95f, 0.95f, 1.00f),
    /* byte_other       */ ImVec4(0.55f, 0.75f, 1.00f, 1.00f),

    /* header_bg        */ ImVec4(0.16f, 0.18f, 0.22f, 1.00f),
    /* header_border    */ ImVec4(0.30f, 0.34f, 0.42f, 1.00f),
    /* header_text      */ ImVec4(0.95f, 0.97f, 1.00f, 1.00f),

    /* zebra            */ ImVec4(1.00f, 1.00f, 1.00f, 0.025f),
    /* search_hit       */ ImVec4(0.20f, 0.70f, 0.25f, 0.55f),
    /* caret_bg         */ ImVec4(0.30f, 0.55f, 0.95f, 0.45f),
    /* caret_text       */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),

    /* btn_primary          */ ImVec4(0.20f, 0.48f, 0.85f, 1.00f),
    /* btn_primary_hover    */ ImVec4(0.30f, 0.58f, 0.95f, 1.00f),
    /* btn_primary_active   */ ImVec4(0.15f, 0.40f, 0.78f, 1.00f),
    /* btn_secondary        */ ImVec4(0.18f, 0.18f, 0.21f, 1.00f),
    /* btn_secondary_hover  */ ImVec4(0.27f, 0.27f, 0.31f, 1.00f),
    /* btn_secondary_active */ ImVec4(0.13f, 0.13f, 0.16f, 1.00f),

    /* status_neutral_bg */ ImVec4(0.20f, 0.22f, 0.27f, 1.00f),
    /* status_neutral_fg */ ImVec4(0.85f, 0.88f, 0.95f, 1.00f),
    /* status_ok_bg      */ ImVec4(0.16f, 0.36f, 0.20f, 1.00f),
    /* status_ok_fg      */ ImVec4(0.80f, 1.00f, 0.85f, 1.00f),
    /* status_warn_bg    */ ImVec4(0.45f, 0.30f, 0.10f, 1.00f),
    /* status_warn_fg    */ ImVec4(1.00f, 0.92f, 0.75f, 1.00f),
    /* status_err_bg     */ ImVec4(0.50f, 0.15f, 0.18f, 1.00f),
    /* status_err_fg     */ ImVec4(1.00f, 0.85f, 0.85f, 1.00f),
    /* status_read_bg    */ ImVec4(0.35f, 0.30f, 0.10f, 1.00f),
    /* status_read_fg    */ ImVec4(1.00f, 0.95f, 0.70f, 1.00f),

    /* help_panel_bg     */ ImVec4(0.13f, 0.15f, 0.19f, 0.96f),
    /* help_panel_border */ ImVec4(0.32f, 0.40f, 0.55f, 0.95f),
    /* help_title_text   */ ImVec4(0.95f, 0.97f, 1.00f, 1.00f),
    /* help_body_text    */ ImVec4(0.62f, 0.66f, 0.74f, 1.00f),
    /* help_close_hover  */ ImVec4(0.55f, 0.20f, 0.20f, 0.85f),
    /* help_close_active */ ImVec4(0.40f, 0.12f, 0.12f, 1.00f),
    /* help_close_glyph  */ ImVec4(0.85f, 0.88f, 0.95f, 1.00f),

    /* start_bg_top     */ ImVec4(0.10f, 0.12f, 0.16f, 1.00f),
    /* start_bg_bottom  */ ImVec4(0.06f, 0.07f, 0.10f, 1.00f),
    /* start_icon       */ ImVec4(0.55f, 0.75f, 1.00f, 0.95f),
    /* start_title_text */ ImVec4(0.95f, 0.97f, 1.00f, 1.00f),
    /* start_error_text */ ImVec4(1.00f, 0.40f, 0.40f, 1.00f),

    /* frame_border_size */ 1.0f,
};

/* Red/green axis shifted to blue/orange. Chrome stays neutral; only
 * semantic colors (status, search, bytes, primary buttons) move. */
const Palette kDeuteranopia = {
    /* text             */ ImVec4(0.94f, 0.95f, 0.97f, 1.00f),
    /* text_disabled    */ ImVec4(0.62f, 0.66f, 0.74f, 1.00f),
    /* frame_bg         */ ImVec4(0.13f, 0.15f, 0.19f, 1.00f),
    /* frame_bg_hovered */ ImVec4(0.20f, 0.24f, 0.32f, 1.00f),
    /* frame_bg_active  */ ImVec4(0.26f, 0.34f, 0.48f, 1.00f),
    /* border           */ ImVec4(0.36f, 0.42f, 0.54f, 1.00f),
    /* nav_cursor       */ ImVec4(0.40f, 0.80f, 1.00f, 1.00f),

    /* byte_zero        */ ImVec4(0.50f, 0.50f, 0.50f, 1.00f),
    /* byte_printable   */ ImVec4(0.95f, 0.95f, 0.95f, 1.00f),
    /* byte_other       */ ImVec4(1.00f, 0.85f, 0.30f, 1.00f),

    /* header_bg        */ ImVec4(0.16f, 0.18f, 0.22f, 1.00f),
    /* header_border    */ ImVec4(0.30f, 0.34f, 0.42f, 1.00f),
    /* header_text      */ ImVec4(0.95f, 0.97f, 1.00f, 1.00f),

    /* zebra            */ ImVec4(1.00f, 1.00f, 1.00f, 0.025f),
    /* search_hit       */ ImVec4(0.95f, 0.55f, 0.10f, 0.60f),
    /* caret_bg         */ ImVec4(0.25f, 0.70f, 0.95f, 0.50f),
    /* caret_text       */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),

    /* btn_primary          */ ImVec4(0.15f, 0.55f, 0.90f, 1.00f),
    /* btn_primary_hover    */ ImVec4(0.25f, 0.65f, 1.00f, 1.00f),
    /* btn_primary_active   */ ImVec4(0.10f, 0.45f, 0.80f, 1.00f),
    /* btn_secondary        */ ImVec4(0.18f, 0.18f, 0.21f, 1.00f),
    /* btn_secondary_hover  */ ImVec4(0.27f, 0.27f, 0.31f, 1.00f),
    /* btn_secondary_active */ ImVec4(0.13f, 0.13f, 0.16f, 1.00f),

    /* status_neutral_bg */ ImVec4(0.20f, 0.22f, 0.27f, 1.00f),
    /* status_neutral_fg */ ImVec4(0.85f, 0.88f, 0.95f, 1.00f),
    /* status_ok_bg      */ ImVec4(0.15f, 0.40f, 0.75f, 1.00f),
    /* status_ok_fg      */ ImVec4(0.80f, 0.92f, 1.00f, 1.00f),
    /* status_warn_bg    */ ImVec4(0.55f, 0.45f, 0.08f, 1.00f),
    /* status_warn_fg    */ ImVec4(1.00f, 0.95f, 0.70f, 1.00f),
    /* status_err_bg     */ ImVec4(0.75f, 0.38f, 0.05f, 1.00f),
    /* status_err_fg     */ ImVec4(1.00f, 0.88f, 0.70f, 1.00f),
    /* status_read_bg    */ ImVec4(0.40f, 0.35f, 0.08f, 1.00f),
    /* status_read_fg    */ ImVec4(1.00f, 0.95f, 0.70f, 1.00f),

    /* help_panel_bg     */ ImVec4(0.13f, 0.15f, 0.19f, 0.96f),
    /* help_panel_border */ ImVec4(0.32f, 0.50f, 0.70f, 0.95f),
    /* help_title_text   */ ImVec4(0.95f, 0.97f, 1.00f, 1.00f),
    /* help_body_text    */ ImVec4(0.62f, 0.66f, 0.74f, 1.00f),
    /* help_close_hover  */ ImVec4(0.75f, 0.40f, 0.05f, 0.85f),
    /* help_close_active */ ImVec4(0.55f, 0.28f, 0.02f, 1.00f),
    /* help_close_glyph  */ ImVec4(0.85f, 0.88f, 0.95f, 1.00f),

    /* start_bg_top     */ ImVec4(0.10f, 0.12f, 0.16f, 1.00f),
    /* start_bg_bottom  */ ImVec4(0.06f, 0.07f, 0.10f, 1.00f),
    /* start_icon       */ ImVec4(0.35f, 0.75f, 1.00f, 0.95f),
    /* start_title_text */ ImVec4(0.95f, 0.97f, 1.00f, 1.00f),
    /* start_error_text */ ImVec4(1.00f, 0.60f, 0.20f, 1.00f),

    /* frame_border_size */ 1.0f,
};

const Palette kHighContrast = {
    /* text             */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* text_disabled    */ ImVec4(0.80f, 0.80f, 0.80f, 1.00f),
    /* frame_bg         */ ImVec4(0.04f, 0.04f, 0.06f, 1.00f),
    /* frame_bg_hovered */ ImVec4(0.14f, 0.14f, 0.20f, 1.00f),
    /* frame_bg_active  */ ImVec4(0.20f, 0.30f, 0.50f, 1.00f),
    /* border           */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* nav_cursor       */ ImVec4(1.00f, 1.00f, 0.20f, 1.00f),

    /* byte_zero        */ ImVec4(0.65f, 0.65f, 0.65f, 1.00f),
    /* byte_printable   */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* byte_other       */ ImVec4(1.00f, 1.00f, 0.30f, 1.00f),

    /* header_bg        */ ImVec4(0.08f, 0.08f, 0.12f, 1.00f),
    /* header_border    */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* header_text      */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),

    /* zebra            */ ImVec4(1.00f, 1.00f, 1.00f, 0.05f),
    /* search_hit       */ ImVec4(1.00f, 1.00f, 0.20f, 0.60f),
    /* caret_bg         */ ImVec4(0.20f, 0.60f, 1.00f, 0.70f),
    /* caret_text       */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),

    /* btn_primary          */ ImVec4(0.10f, 0.50f, 0.95f, 1.00f),
    /* btn_primary_hover    */ ImVec4(0.25f, 0.65f, 1.00f, 1.00f),
    /* btn_primary_active   */ ImVec4(0.05f, 0.40f, 0.85f, 1.00f),
    /* btn_secondary        */ ImVec4(0.08f, 0.08f, 0.12f, 1.00f),
    /* btn_secondary_hover  */ ImVec4(0.20f, 0.20f, 0.28f, 1.00f),
    /* btn_secondary_active */ ImVec4(0.05f, 0.05f, 0.08f, 1.00f),

    /* status_neutral_bg */ ImVec4(0.10f, 0.10f, 0.14f, 1.00f),
    /* status_neutral_fg */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* status_ok_bg      */ ImVec4(0.08f, 0.50f, 0.15f, 1.00f),
    /* status_ok_fg      */ ImVec4(0.90f, 1.00f, 0.90f, 1.00f),
    /* status_warn_bg    */ ImVec4(0.60f, 0.45f, 0.05f, 1.00f),
    /* status_warn_fg    */ ImVec4(1.00f, 1.00f, 0.85f, 1.00f),
    /* status_err_bg     */ ImVec4(0.70f, 0.10f, 0.15f, 1.00f),
    /* status_err_fg     */ ImVec4(1.00f, 0.95f, 0.95f, 1.00f),
    /* status_read_bg    */ ImVec4(0.55f, 0.40f, 0.05f, 1.00f),
    /* status_read_fg    */ ImVec4(1.00f, 1.00f, 0.80f, 1.00f),

    /* help_panel_bg     */ ImVec4(0.04f, 0.04f, 0.06f, 0.98f),
    /* help_panel_border */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* help_title_text   */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* help_body_text    */ ImVec4(0.85f, 0.85f, 0.85f, 1.00f),
    /* help_close_hover  */ ImVec4(0.70f, 0.10f, 0.15f, 0.90f),
    /* help_close_active */ ImVec4(0.55f, 0.05f, 0.10f, 1.00f),
    /* help_close_glyph  */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),

    /* start_bg_top     */ ImVec4(0.02f, 0.02f, 0.04f, 1.00f),
    /* start_bg_bottom  */ ImVec4(0.00f, 0.00f, 0.00f, 1.00f),
    /* start_icon       */ ImVec4(1.00f, 1.00f, 0.30f, 1.00f),
    /* start_title_text */ ImVec4(1.00f, 1.00f, 1.00f, 1.00f),
    /* start_error_text */ ImVec4(1.00f, 0.40f, 0.40f, 1.00f),

    /* frame_border_size */ 2.0f,
};

const Palette& Active(GuiState::Palette p) {
    switch (p) {
        case GuiState::PAL_DEUTERANOPIA:  return kDeuteranopia;
        case GuiState::PAL_HIGH_CONTRAST: return kHighContrast;
        case GuiState::PAL_DEFAULT:
        default:                          return kDefault;
    }
}

const char* Name(GuiState::Palette p) {
    switch (p) {
        case GuiState::PAL_DEUTERANOPIA:  return "Deuteranopia";
        case GuiState::PAL_HIGH_CONTRAST: return "High contrast";
        case GuiState::PAL_DEFAULT:
        default:                          return "Default";
    }
}

void PushEditorStyle(const Palette& p) {
    ImGui::PushStyleColor(ImGuiCol_Text,           p.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,   p.text_disabled);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        p.frame_bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, p.frame_bg_hovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  p.frame_bg_active);
    ImGui::PushStyleColor(ImGuiCol_Border,         p.border);
    ImGui::PushStyleColor(ImGuiCol_NavCursor,      p.nav_cursor);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameBorderSize, p.frame_border_size);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding,   layout::kFrameRounding);
    ImGui::PushStyleVar  (ImGuiStyleVar_ItemSpacing,
                          ImVec2(layout::kItemSpacingX, layout::kItemSpacingY));
}

void PopEditorStyle() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
}

ImVec4 ColorForByte(const Palette& p, unsigned char b) {
    if (b == 0x00) return p.byte_zero;
    if (b >= 0x20 && b <= 0x7E) return p.byte_printable;
    return p.byte_other;
}

} /* namespace ui::theme */
