#pragma once

#include "hex_editor_core.h"
#include "app_state.h"

#include <string>

struct ImFont;

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

/* core is non-null iff state == HexView. out_pending_path receives the
 * Select File button's result, consumed by the main loop like a GLFW drop.
 * out_installer_to_launch receives the path of a verified update installer
 * when the user clicks "Install and restart" in Settings; the main loop
 * hands off to updater-helper.exe and shuts down. drag_over_state encodes
 * the OS drag-hover state (0=none, 1=valid file, 2=rejected e.g. folder);
 * only meaningful on the start screen. */
void RenderHexEditorUI(AppState state,
                       HexEditorCore* core,
                       const char* load_error,
                       std::string* out_pending_path,
                       std::string* out_installer_to_launch,
                       int drag_over_state);
