/* status_bar.h — Bottom status bar with mode badges, transient messages,
 * and the contextual hint that follows keyboard focus. */

#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"
#include "ui/theme.h"

#include "imgui.h"

namespace ui {

/* Small colored pill aligned to a frame-padded text baseline. */
void Badge(const char* text, ImVec4 bg, ImVec4 fg, float alpha = 1.0f);

const char* GetContextualHint(const GuiState& s, const HexEditorCore& core);

void RenderStatusBar(GuiState& s, const theme::Palette& pal, HexEditorCore& core);

} /* namespace ui */
