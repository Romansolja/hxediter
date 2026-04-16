/* gui_state.h — Editor UI state.
 *
 * Holds everything that used to live in gui.cpp's anonymous-namespace
 * globals. A single instance is owned by gui.cpp and passed by reference
 * to every renderer under src/ui/. */

#pragma once

#include <cstdint>
#include <string>

struct ImFont;

namespace ui {

struct GuiState {
    enum StatusKind { STATUS_INFO, STATUS_OK, STATUS_WARN, STATUS_ERROR };
    enum FocusField { FOCUS_NONE, FOCUS_GOTO, FOCUS_SEARCH, FOCUS_BYTE };
    enum Palette    { PAL_DEFAULT, PAL_DEUTERANOPIA, PAL_HIGH_CONTRAST, PAL_COUNT };

    /* --- Toolbar input buffers */
    char goto_buf[17]   = "";
    char search_buf[64] = "";

    /* --- Byte grid selection & edit */
    int64_t selected_byte = -1;   /* offset being inline-edited, or -1 */
    int64_t caret_byte    = -1;   /* focused byte (persists past edit) */
    char    edit_buf[3]   = "";
    bool    focus_edit    = false;
    int64_t last_hit      = -1;

    /* --- Status bar / contextual hint */
    std::string status_msg;
    float       status_timer  = 0.0f;
    StatusKind  status_kind   = STATUS_INFO;
    bool        status_sticky = false;  /* pin until user clicks the x */
    FocusField  focus_field   = FOCUS_NONE;

    /* --- External-modification watch.
     * Set each frame by polling HexEditorCore::HasExternalModification.
     * Once set, it stays set until the user resolves it (Reload or
     * Keep-mine). The conflict modal opens when `conflict_modal_open`
     * is true — we don't drive it directly from the flag because the
     * user may want to dismiss the modal and come back to it. */
    bool externally_modified = false;
    bool conflict_modal_open = false;
    /* Pending byte edit that was intercepted by the conflict gate.
     * pending_edit_offset is -1 when no edit is waiting. */
    int64_t       pending_edit_offset = -1;
    unsigned char pending_edit_value  = 0;

    /* --- Help / onboarding */
    bool  show_help       = true;
    bool  user_interacted = false;
    float help_anim       = 1.0f;

    /* --- Fonts (set once from main.cpp via SetEditorFonts) */
    ImFont* ui_font    = nullptr;
    ImFont* mono_font  = nullptr;
    ImFont* title_font = nullptr;
    ImFont* icon_font  = nullptr;

    /* --- Accessibility (session-only; reset on launch) */
    float   font_scale = 1.0f;
    Palette palette    = PAL_DEFAULT;

    void SetStatus(std::string msg, StatusKind kind = STATUS_INFO,
                   bool sticky = false);
    void MarkInteracted();
};

} /* namespace ui */
