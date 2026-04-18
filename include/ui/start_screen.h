#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

#include <string>

namespace ui {

/* drag_over_state: 0 = no drag, 1 = valid file hovering, 2 = rejected
 * (folder / unreadable). Drives the drop-zone overlay; the animation
 * lerp lives in GuiState::drag_overlay_anim. */
void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error, std::string* out_pending_path,
                       int drag_over_state);

} /* namespace ui */
