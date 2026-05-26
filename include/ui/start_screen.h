#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

#include <string>
#include <vector>

namespace ui {

// drag_over_state: 0=none, 1=valid hover, 2=rejected.
void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       int drag_over_state,
                       std::vector<std::string>* out_pending_directories);

}
