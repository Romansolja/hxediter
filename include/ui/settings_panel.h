#pragma once

#include "ui/gui_state.h"

#include <string>

namespace ui {

/* Renders the Settings modal popup. Must be called every frame; the popup
 * itself only draws when OpenPopup("Settings##settings") has fired.
 *
 * out_installer_to_launch: written non-empty (absolute path to NSIS
 * installer) when the user clicks "Install and restart" and the download
 * + integrity check have succeeded. Main loop owns the path and spawns
 * updater-helper.exe. */
void RenderSettingsPopup(GuiState& s, std::string* out_installer_to_launch);

} /* namespace ui */
