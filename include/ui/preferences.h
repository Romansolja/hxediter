#pragma once

#include "ui/gui_state.h"

namespace ui {

// Apply prefs.txt onto s. Missing file/keys → defaults; out-of-range values clamped.
void LoadPreferences(GuiState& s);

// Snapshot s to disk. Failures logged to stderr; never throws.
void SavePreferences(const GuiState& s);

}
