/* gui.h — Dear ImGui hex editor view */

#pragma once

#include "hex_editor_core.h"
#include "app_state.h"

#include <string>

struct ImFont;

/* Set the four fonts used by the editor. All may be null — the UI falls back
 * gracefully (small elements use the default font, the start-screen icon
 * degrades to a drawn rectangle when icon_font is null). */
void SetEditorFonts(ImFont* ui_font,
                    ImFont* mono_font,
                    ImFont* title_font,
                    ImFont* icon_font);

/* Render one frame of the editor UI. Call between ImGui::NewFrame() and
 * ImGui::Render() in the main loop.
 *
 * state           — which screen to render (StartScreen or HexView).
 * core            — non-null iff state == HexView.
 * load_error      — optional message shown on the start screen (may be null).
 * out_pending_path— non-null; the start screen's Select File button writes
 *                   the chosen path here, and the main loop consumes it on
 *                   the next iteration the same way it consumes GLFW drops. */
void RenderHexEditorUI(AppState state,
                       HexEditorCore* core,
                       const char* load_error,
                       std::string* out_pending_path);
