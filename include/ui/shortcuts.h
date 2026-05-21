#pragma once

#include "imgui.h"

namespace ui {

/* "Shortcut modifier" held this frame — what app code should check before
 * matching chord keys like Cmd/Ctrl+Z, Cmd/Ctrl+W, Cmd/Ctrl+Tab, etc.
 *
 * macOS (ConfigMacOSXBehaviors == true): accept *either* Cmd or Ctrl.
 *   Cmd is idiomatic; Ctrl is a kindness to users coming from Windows
 *   muscle memory and to anyone whose keyboard layer reports Cmd as
 *   Ctrl (Karabiner, hidutil, some Bluetooth firmware).
 * Windows / Linux: Ctrl only. Super-key chords belong to the window
 *   manager (Start menu, Activities, etc.) and shouldn't be claimed by
 *   the app.
 *
 * If the policy ever needs to change (e.g., adding Alt on Linux for
 * GNOME conventions), this is the single edit point. */
inline bool ShortcutHeld(const ImGuiIO& io) {
    return io.ConfigMacOSXBehaviors
             ? (io.KeySuper || io.KeyCtrl)
             : io.KeyCtrl;
}

} /* namespace ui */
