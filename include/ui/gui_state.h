#pragma once

#include <cstdint>
#include <string>

struct ImFont;

namespace ui {

struct GuiState {
    enum StatusKind { STATUS_INFO, STATUS_OK, STATUS_WARN, STATUS_ERROR };
    enum FocusField { FOCUS_NONE, FOCUS_GOTO, FOCUS_SEARCH, FOCUS_BYTE };
    enum Palette    { PAL_DEFAULT, PAL_DEUTERANOPIA, PAL_HIGH_CONTRAST, PAL_COUNT };

    std::string status_msg;
    float       status_timer  = 0.0f;
    StatusKind  status_kind   = STATUS_INFO;
    bool        status_sticky = false;  /* pin until user clicks the x */

    bool  show_help       = true;
    bool  user_interacted = false;
    float help_anim       = 1.0f;

    /* Lerped toward 1 while an OS drag is hovering the start screen; 0
     * otherwise. Drives the drop-zone overlay fade. */
    float drag_overlay_anim = 0.0f;

    /* Set by the toolbar gear button; consumed by the popup trigger in
     * RenderHexEditorUI. Mirrors the conflict_modal_open one-shot pattern. */
    bool show_settings = false;

    /* When true, main.cpp forces each newly opened file into read-only
     * mode regardless of filesystem permissions. Already-open files
     * are not retroactively affected when this is toggled. */
    bool readonly_default = false;

    /* When true and the window is unfocused, main.cpp swaps
     * glfwPollEvents for glfwWaitEventsTimeout(1/15) so the editor
     * drops to ~15 FPS while idle in the background. Any incoming
     * event wakes the loop immediately. */
    bool background_throttle = true;

    ImFont* ui_font         = nullptr;
    ImFont* mono_font       = nullptr;
    ImFont* title_font      = nullptr;
    ImFont* icon_font       = nullptr;
    ImFont* icon_font_small = nullptr;  /* narrow FA range at toolbar size */

    float   font_scale = 1.0f;
    /* HiDPI multiplier baked at startup from glfwGetWindowContentScale.
     * UI code multiplies hardcoded pixel layout constants (panel widths,
     * padding, etc.) by this to keep proportions on 4K / high-DPI panels.
     * Separate from font_scale so the user's zoom stays orthogonal. */
    float   content_scale = 1.0f;
    Palette palette    = PAL_DEFAULT;

    float startup_duration_ms = 0.0f;
    bool  startup_measured    = false;

    /* Last tab index the bar saw selected. The render uses the delta with
     * `*active_doc` to apply ImGuiTabItemFlags_SetSelected exactly once
     * after a programmatic switch (Ctrl+Tab, tab close), without overriding
     * user clicks on subsequent frames. */
    int last_tab_active_seen = -1;

    void SetStatus(std::string msg, StatusKind kind = STATUS_INFO,
                   bool sticky = false);
    void MarkInteracted();
};

/* Per-open-file editor state. One DocumentState is paired with each
 * HexEditorCore in the AppContext::docs vector; the active tab's pair
 * is the one the render helpers see this frame. */
struct DocumentState {
    int64_t selected_byte = -1;   /* offset being inline-edited, or -1 */
    int64_t caret_byte    = -1;   /* focused byte (persists past edit) */
    char    edit_buf[3]   = "";
    bool    focus_edit    = false;
    int64_t last_hit      = -1;

    char goto_buf[17]   = "";
    char search_buf[64] = "";

    GuiState::FocusField focus_field = GuiState::FOCUS_NONE;

    /* Latches on drift and stays set until the user resolves via the
     * conflict modal (Reload or Keep-mine). */
    bool          externally_modified = false;
    bool          conflict_modal_open = false;
    int64_t       pending_edit_offset = -1;  /* -1 = no edit waiting */
    unsigned char pending_edit_value  = 0;

    /* When >= 0, the next render of the hex grid scrolls so this offset
     * sits ~30% from the top of the body. Cleared after applied. Set by
     * Goto/Search and by keyboard nav that would otherwise move the caret
     * off-screen. */
    int64_t pending_scroll_offset = -1;
};

} /* namespace ui */
