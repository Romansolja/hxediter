#pragma once

#include "ui/gui_state.h"
#include "ui/theme.h"

#include <string>
#include <vector>

namespace ui {

/* drag_over_state: 0 = no drag, 1 = valid file hovering, 2 = rejected
 * (folder / unreadable). Drives the drop-zone overlay; the animation
 * lerp lives in GuiState::drag_overlay_anim.
 * out_pending_paths receives the path chosen by the Select File dialog
 * (appended — the main loop consumes the whole vector each tick).
 * out_pending_directories receives the path chosen by the Open Folder
 * dialog; main.cpp expands it into directory_files for the dropdown.
 * out_pending_triage_root receives the path chosen by the Triage Folder
 * dialog; main.cpp transitions AppState to FolderTriage and kicks off
 * triage::StartScan when this is non-empty. */
void RenderStartScreen(GuiState& s, const theme::Palette& pal,
                       const char* load_error,
                       std::vector<std::string>* out_pending_paths,
                       int drag_over_state,
                       std::vector<std::string>* out_pending_directories,
                       std::vector<std::string>* out_pending_triage_root);

} /* namespace ui */
