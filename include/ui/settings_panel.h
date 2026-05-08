#pragma once

#include "ui/gui_state.h"

#include <string>

namespace ui {

/* Renders the Settings modal popup. Must be called every frame; the popup
 * itself only draws when OpenPopup("Settings##settings") has fired.
 *
 * out_installer_to_launch / out_installer_sha256: both written non-empty
 * (absolute path + lowercase-hex SHA256) when the user clicks "Install
 * and restart" and the download + integrity check have succeeded. Main
 * loop owns the path and spawns updater-helper.exe; the helper re-checks
 * the SHA256 right before elevation to close the %TEMP% TOCTOU window. */
void RenderSettingsPopup(GuiState& s,
                         std::string* out_installer_to_launch,
                         std::string* out_installer_sha256);

} /* namespace ui */
