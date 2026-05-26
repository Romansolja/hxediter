#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

#include <string>
#include <vector>

namespace ui {

// Drag-hover overlay used to live here, but GLFW only fires on drop, not on
// hover — restoring it would need macOS NSDraggingDestination wiring through
// a custom NSView. Dropped until that arrives so the dead `drag_over_state`
// parameter doesn't mislead readers into thinking the overlay still works.
void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       std::vector<std::string>* out_pending_directories);

}
