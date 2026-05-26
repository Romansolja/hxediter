#pragma once

#include "hex_editor_core.h"
#include "app_state.h"
#include "ui/gui_state.h"

#include <memory>
#include <string>
#include <vector>

struct ImFont;

struct OpenDocument {
    std::unique_ptr<HexEditorCore> core;
    ui::DocumentState              doc_state;
};

// Any font may be null — UI falls back. icon_font_small is a narrow-range FA at toolbar size.
void SetEditorFonts(ImFont* ui_font,
                    ImFont* mono_font,
                    ImFont* title_font,
                    ImFont* icon_font,
                    ImFont* icon_font_small);

void SetStartupDuration(float duration_ms);

// One-shot at startup with glfwGetWindowContentScale().
void SetContentScale(float scale);

// Apply persisted prefs to GuiState. Call before the first frame; missing file = defaults.
void LoadGuiPreferences();

// Snapshot current prefs to disk. Logs errors to stderr; never throws.
void SaveGuiPreferences();

bool ReadonlyDefault();
bool BackgroundThrottle();

// Push a batch-load error or limit message onto the active tab's status bar.
void SetExternalStatus(const std::string& msg, bool is_error);

// out_pending_paths/_directories/_close_indices are drained by the main loop.
// out_clear_directory: UI sets true to ask main to forget the current folder.
void RenderHexEditorUI(AppState state,
                       std::vector<OpenDocument>* docs,
                       int* active_doc,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       std::vector<int>* out_close_indices,
                       const std::vector<std::string>* directory_files,
                       const std::string* directory_label,
                       bool* out_clear_directory,
                       std::vector<std::string>* out_pending_directories);
