#pragma once

#include "ui/gui_state.h"

namespace ui {

// Call every frame — draws only when OpenPopup("Settings##settings") has fired.
void RenderSettingsPopup(GuiState& s);

}
