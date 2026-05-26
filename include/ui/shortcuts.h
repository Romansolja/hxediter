#pragma once

#include "imgui.h"

namespace ui {

// Cmd (canonical on macOS) or Ctrl (Windows muscle memory + Karabiner-style remappers).
inline bool ShortcutHeld(const ImGuiIO& io) {
    return io.KeySuper || io.KeyCtrl;
}

}
