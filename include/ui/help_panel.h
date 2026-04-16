/* help_panel.h — Quick reference panel drawn inside the grid's leftover
 * vertical space until the user starts working. */

#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

namespace ui {

void RenderHelpPanel(GuiState& s, const theme::Palette& pal, float visibility);

} /* namespace ui */
