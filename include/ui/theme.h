/* theme.h — Color palettes for the editor UI.
 *
 * All renderers read colors from a Palette passed by const reference.
 * Three palettes are defined in theme.cpp:
 *   kDefault       — today's look, literal-for-literal
 *   kDeuteranopia  — red/green axis shifted to blue/orange for
 *                    deuteranopia-safe contrast
 *   kHighContrast  — boosted luminance, heavier frame border, for
 *                    low-vision users
 *
 * Active() selects one by GuiState::Palette enum.
 * PushEditorStyle() / PopEditorStyle() handle the ImGui style stack
 * that gui.cpp used to push inline at RenderHexEditorUI entry. */

#pragma once

#include "imgui.h"
#include "ui/gui_state.h"

namespace ui::theme {

struct Palette {
    /* Chrome / frames */
    ImVec4 text, text_disabled;
    ImVec4 frame_bg, frame_bg_hovered, frame_bg_active, border, nav_cursor;

    /* Byte display */
    ImVec4 byte_zero, byte_printable, byte_other;

    /* Hex grid header */
    ImVec4 header_bg, header_border, header_text;

    /* Grid body */
    ImVec4 zebra, search_hit, caret_bg, caret_text;

    /* Toolbar buttons */
    ImVec4 btn_primary, btn_primary_hover, btn_primary_active;
    ImVec4 btn_secondary, btn_secondary_hover, btn_secondary_active;

    /* Status bar badges */
    ImVec4 status_neutral_bg, status_neutral_fg;
    ImVec4 status_ok_bg,      status_ok_fg;
    ImVec4 status_warn_bg,    status_warn_fg;
    ImVec4 status_err_bg,     status_err_fg;
    ImVec4 status_read_bg,    status_read_fg;

    /* Help panel */
    ImVec4 help_panel_bg, help_panel_border;
    ImVec4 help_title_text, help_body_text;
    ImVec4 help_close_hover, help_close_active, help_close_glyph;

    /* Start screen */
    ImVec4 start_bg_top, start_bg_bottom;
    ImVec4 start_icon, start_title_text, start_error_text;

    /* Per-palette frame border thickness (high contrast bumps this) */
    float  frame_border_size;
};

extern const Palette kDefault;
extern const Palette kDeuteranopia;
extern const Palette kHighContrast;

const Palette& Active(GuiState::Palette p);
const char*    Name  (GuiState::Palette p);

/* Pushes ImGuiCol_Text / TextDisabled / FrameBg* / Border / NavCursor plus
 * the FrameBorderSize / FrameRounding / ItemSpacing style vars. Must be
 * matched by PopEditorStyle(). */
void PushEditorStyle(const Palette& p);
void PopEditorStyle();

/* Byte text color, swappable per palette. */
ImVec4 ColorForByte(const Palette& p, unsigned char b);

} /* namespace ui::theme */
