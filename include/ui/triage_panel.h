#pragma once

/* ui::RenderTriagePanel — folder-triage screen.
 *
 * Activated when AppState == FolderTriage. The panel reads its
 * scan-progress state from the global triage::scanner singleton; it
 * does not require main.cpp to thread scan results through. Move
 * operations spawn a worker thread internally so the UI stays
 * responsive on multi-tens-of-thousands moves. */

#include "ui/gui_state.h"

namespace ui {

/* Render the triage panel for one frame.
 *
 * `out_request_back`, if non-null, is set to true when the user clicks
 * the "Back" button (or otherwise asks to leave the panel). The main
 * loop should observe this and transition AppState to StartScreen.
 *
 * The panel currently uses ImGui defaults rather than the editor's
 * theme palette — when the table needs themed colors (verdict badges
 * use hardcoded RGBA today), thread theme::Palette in then. */
void RenderTriagePanel(GuiState& s, bool* out_request_back);

}  /* namespace ui */
