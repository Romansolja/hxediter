/* gui.h — Dear ImGui hex editor view */

#pragma once

#include "hex_editor_core.h"

struct ImFont;

/* Render one frame of the hex editor UI. Call between ImGui::NewFrame() and
 * ImGui::Render() in the main loop. */
void SetEditorFonts(ImFont* ui_font, ImFont* mono_font);
/* core may be null — pass null to render the drop-zone welcome screen.
 * load_error is an optional message shown on the welcome screen (may be null
 * or empty). */
void RenderHexEditorUI(HexEditorCore* core, const char* load_error);
