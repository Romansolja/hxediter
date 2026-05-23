#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

#include <string>
#include <vector>

namespace ui {

// drag_over_state: 0=no drag, 1=valid file hover, 2=rejected.
// out_pending_paths is appended with Select-File dialog results (main
// loop drains it each tick); out_pending_directories receives the
// Open-Folder result for main.cpp to expand into directory_files.
void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       int drag_over_state,
                       std::vector<std::string>* out_pending_directories);

} // namespace ui
