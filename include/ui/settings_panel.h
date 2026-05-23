#pragma once

#include "ui/gui_state.h"

namespace ui {

// Renders the Settings modal popup. Must be called every frame; popup
// only draws when OpenPopup("Settings##settings") has fired.
void RenderSettingsPopup(GuiState& s);

} // namespace ui
