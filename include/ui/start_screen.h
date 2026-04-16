/* start_screen.h — Branded landing screen shown when no file is loaded. */

#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

#include <string>

namespace ui {

void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error, std::string* out_pending_path);

} /* namespace ui */
