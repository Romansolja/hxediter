#pragma once

#include "hex_editor_core.h"
#include "app_state.h"
#include "ui/gui_state.h"

#include <memory>
#include <string>
#include <vector>

struct ImFont;

/* One open file. Pair of the editor core with its per-file UI state.
 * Main owns a vector<OpenDocument>; the render entry picks one as active. */
struct OpenDocument {
    std::unique_ptr<HexEditorCore> core;
    ui::DocumentState              doc_state;
};

/* Any font may be null — UI falls back (default font for text; start-screen
 * icon degrades to a drawn rectangle when icon_font is null).
 * icon_font_small is a narrow-range FA atlas sized for toolbar glyphs. */
void SetEditorFonts(ImFont* ui_font,
                    ImFont* mono_font,
                    ImFont* title_font,
                    ImFont* icon_font,
                    ImFont* icon_font_small);

void SetStartupDuration(float duration_ms);

/* Called once at startup with the value read from
 * glfwGetWindowContentScale. Stored on GuiState so per-widget code
 * (help panel width, etc.) can multiply hardcoded pixel constants. */
void SetContentScale(float scale);

/* Snapshot of the GuiState read-only default toggle. main.cpp consults
 * this right after constructing a HexEditorCore so the "Open files as
 * read-only" setting applies to newly loaded files. */
bool ReadonlyDefault();

/* Snapshot of the GuiState background-throttle toggle. main.cpp reads
 * it once per loop iteration to decide between glfwPollEvents and
 * glfwWaitEventsTimeout when the window is unfocused. */
bool BackgroundThrottle();

/* main.cpp uses this to surface batch-load errors and limits (e.g. tab cap
 * hit) on the active tab's status bar. is_error picks the status color. */
void SetExternalStatus(const std::string& msg, bool is_error);

/* docs is the list of open files; *active_doc is the index of the one
 * whose body we render this frame (may be updated by the tab bar and
 * returned to the main loop). out_pending_paths receives paths to open
 * (from the Select File button or passthrough); consumed by the main
 * loop like a GLFW drop. out_installer_to_launch receives the path of a
 * verified update installer when the user clicks "Install and restart" in
 * Settings. drag_over_state encodes the OS drag-hover state (0=none,
 * 1=valid, 2=rejected); meaningful on the start screen. out_close_indices
 * is populated with tab indices the user asked to close (via the tab X,
 * middle-click, or Ctrl+W); the main loop erases them.
 *
 * directory_files is the alphabetical list of files in the most-recently
 * loaded folder (empty if no folder loaded). directory_label is the
 * folder's basename — both feed the tab-bar dropdown so users can pick
 * which file(s) to open from the directory. out_clear_directory, when
 * non-null and set true by the UI, signals the main loop to forget the
 * current directory listing (e.g. user clicked "Close folder").
 *
 * out_pending_triage_root receives a folder path when the user picks
 * one via the start screen's "Triage Folder..." button; main.cpp
 * transitions to AppState::FolderTriage and kicks off triage::StartScan.
 * out_request_triage_back is set true when the triage panel's Back
 * button is clicked; main.cpp transitions back to StartScreen. */
void RenderHexEditorUI(AppState state,
                       std::vector<OpenDocument>* docs,
                       int* active_doc,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       std::string* out_installer_to_launch,
                       int drag_over_state,
                       std::vector<int>* out_close_indices,
                       const std::vector<std::string>* directory_files,
                       const std::string* directory_label,
                       bool* out_clear_directory,
                       std::vector<std::string>* out_pending_triage_root,
                       bool* out_request_triage_back);
