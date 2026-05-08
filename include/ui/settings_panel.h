#pragma once

#include "ui/gui_state.h"

#include <string>

namespace ui {

/* Renders the Settings modal popup. Must be called every frame; the popup
 * itself only draws when OpenPopup("Settings##settings") has fired.
 *
 * The popup does NOT carry the verified-installer path back through an
 * out-param — the main loop polls updater::ConsumeInstallerPath() once
 * per frame regardless of popup visibility, and is the single owner of
 * the launch handoff. The popup just closes itself when the integrity
 * check succeeds, so the user's "Install and restart" click feels acted
 * on; main picks up the path on the same frame. */
void RenderSettingsPopup(GuiState& s);

} /* namespace ui */
