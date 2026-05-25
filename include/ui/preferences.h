#pragma once

#include "ui/gui_state.h"

namespace ui {

// Load persisted user preferences (sliders, palette, toggles) from
// platform::AppSupportDir()+"prefs.txt" and write them onto `s`. Missing
// file or missing keys leave the corresponding field at its compiled-in
// default — no error. Out-of-range values are clamped.
void LoadPreferences(GuiState& s);

// Write the current values of persisted fields to disk. Safe to call from
// the shutdown path; failures are logged to stderr and otherwise ignored.
void SavePreferences(const GuiState& s);

} // namespace ui
