#pragma once

#include "imgui.h"

namespace ui {

// Shortcut modifier held this frame — checked before matching chords
// like Cmd/Ctrl+Z, Cmd/Ctrl+W, Cmd/Ctrl+Tab. Cmd is idiomatic on macOS;
// Ctrl is accepted as a kindness to users with Windows muscle memory
// and to anyone whose keyboard layer reports Cmd as Ctrl (Karabiner,
// hidutil, some Bluetooth firmware).
inline bool ShortcutHeld(const ImGuiIO& io) {
    return io.KeySuper || io.KeyCtrl;
}

} // namespace ui
