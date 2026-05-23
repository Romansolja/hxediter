#pragma once

#include "hex_editor_core.h"
#include "app_state.h"
#include "ui/gui_state.h"

#include <memory>
#include <string>
#include <vector>

struct ImFont;

// One open file. Editor core paired with its per-file UI state. Main owns
// vector<OpenDocument>; the render entry picks one as active.
struct OpenDocument {
    std::unique_ptr<HexEditorCore> core;
    ui::DocumentState              doc_state;
};

// Any font may be null — UI falls back (default font for text; start-screen
// icon degrades to a drawn rectangle when icon_font is null).
// icon_font_small is a narrow-range FA atlas sized for toolbar glyphs.
void SetEditorFonts(ImFont* ui_font,
                    ImFont* mono_font,
                    ImFont* title_font,
                    ImFont* icon_font,
                    ImFont* icon_font_small);

void SetStartupDuration(float duration_ms);

// Called once at startup with the glfwGetWindowContentScale value. Stored
// on GuiState so per-widget code can multiply hardcoded pixel constants.
void SetContentScale(float scale);

// Snapshot of the GuiState read-only default toggle. main.cpp consults
// this after constructing a HexEditorCore so "Open files as read-only"
// applies to newly loaded files.
bool ReadonlyDefault();

// Snapshot of the GuiState background-throttle toggle. main.cpp reads it
// once per loop iteration to decide between glfwPollEvents and
// glfwWaitEventsTimeout when the window is unfocused.
bool BackgroundThrottle();

// Surfaces batch-load errors and limits (e.g. tab cap hit) on the active
// tab's status bar. is_error picks the status color.
void SetExternalStatus(const std::string& msg, bool is_error);

// docs: open files. *active_doc: index of the one we render this frame
// (may be updated by the tab bar). out_pending_paths: paths to open (from
// Select File button or passthrough); drained by the main loop like a
// GLFW drop. drag_over_state: 0=none, 1=valid, 2=rejected — meaningful on
// the start screen. out_close_indices: tab indices the user asked to close
// (tab X, middle-click, Ctrl+W); main loop erases them.
//
// directory_files: alphabetical list of files in the most-recently loaded
// folder (empty if none). directory_label: folder basename — both feed the
// tab-bar dropdown. out_clear_directory: when non-null and set true by the
// UI, signals main to forget the current listing (e.g. "Close folder").
void RenderHexEditorUI(AppState state,
                       std::vector<OpenDocument>* docs,
                       int* active_doc,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       int drag_over_state,
                       std::vector<int>* out_close_indices,
                       const std::vector<std::string>* directory_files,
                       const std::string* directory_label,
                       bool* out_clear_directory,
                       std::vector<std::string>* out_pending_directories);
