/* gui.h — Dear ImGui hex editor view */

#pragma once

#include "hex_editor_core.h"

struct ImFont;

/* Render one frame of the hex editor UI. Call between ImGui::NewFrame() and
 * ImGui::Render() in the main loop. */
void SetEditorFonts(ImFont* ui_font, ImFont* mono_font);
void RenderHexEditorUI(HexEditorCore& core);
