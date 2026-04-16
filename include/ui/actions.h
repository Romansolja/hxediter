/* actions.h — Hex parsing helpers and editor command handlers. */

#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"

#include <cstdint>
#include <vector>

namespace ui {

bool ParseHexU64(const char* s, uint64_t* out);
std::vector<unsigned char> ParseHexBytes(const char* s);

void DoGoto    (GuiState& s, HexEditorCore& core);
void DoSearch  (GuiState& s, HexEditorCore& core);
void DoUndo    (GuiState& s, HexEditorCore& core);
void CommitEdit(GuiState& s, HexEditorCore& core);

} /* namespace ui */
