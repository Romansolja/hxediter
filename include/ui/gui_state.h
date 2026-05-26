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
    bool        status_sticky = false;

    bool  show_help       = true;
    bool  user_interacted = false;
    float help_anim       = 1.0f;

    float drag_overlay_anim = 0.0f;     // lerped toward 1 while an OS drag hovers

    bool show_settings = false;

    // Only applies to newly opened files — toggling does not retro-affect open tabs.
    bool readonly_default = false;

    bool background_throttle = true;

    ImFont* ui_font         = nullptr;
    ImFont* mono_font       = nullptr;
    ImFont* title_font      = nullptr;
    ImFont* icon_font       = nullptr;
    ImFont* icon_font_small = nullptr;

    float   font_scale = 1.0f;          // hex grid zoom
    float   chrome_scale = 0.85f;       // toolbar/tab/status/start-screen zoom (independent)
    float   content_scale = 1.0f;       // HiDPI multiplier baked at startup
    Palette palette    = PAL_DEFAULT;

    float startup_duration_ms = 0.0f;
    bool  startup_measured    = false;

    // Delta with *active_doc triggers ImGuiTabItemFlags_SetSelected exactly once after
    // a programmatic switch — every-frame SetSelected would override user clicks.
    int last_tab_active_seen = -1;

    void SetStatus(std::string msg, StatusKind kind = STATUS_INFO,
                   bool sticky = false);
    void MarkInteracted();
};

// Per-open-file editor state, paired with each HexEditorCore.
struct DocumentState {
    int64_t selected_byte = -1;   // offset being inline-edited, or -1
    int64_t caret_byte    = -1;   // focused byte (persists past edit)
    char    edit_buf[3]   = "";
    bool    focus_edit    = false;
    int64_t last_hit      = -1;

    char goto_buf[17]   = "";
    char search_buf[64] = "";

    GuiState::FocusField focus_field = GuiState::FOCUS_NONE;

    // externally_modified latches until the conflict modal resolves it.
    // pending_edit_* / pending_undo defer an action triggered while the modal is up.
    bool          externally_modified = false;
    bool          conflict_modal_open = false;
    int64_t       pending_edit_offset = -1;
    unsigned char pending_edit_value  = 0;
    bool          pending_undo        = false;

    // Next render scrolls so this offset sits ~30% from the top, then clears.
    int64_t pending_scroll_offset = -1;
};

}
