/* gui.h — Dear ImGui hex editor view */

#pragma once

#include "hex_editor_core.h"

/* Render one frame of the hex editor UI. Call between ImGui::NewFrame() and
 * ImGui::Render() in the main loop. */
void RenderHexEditorUI(HexEditorCore& core);
