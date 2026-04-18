#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"
#include "ui/theme.h"

namespace ui {

void RenderToolbar(GuiState& s, const theme::Palette& pal, HexEditorCore& core);

} /* namespace ui */
