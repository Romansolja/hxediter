#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"
#include "ui/theme.h"

#include <vector>

namespace ui {

// Precomputed column x-coordinates shared by header strip and body so
// both land on the exact same pixels.
struct HexLayout {
    int   bytes_per_line;
    float char_w;
    float byte_w;
    float offset_w;
    std::vector<float> byte_x;
    float ascii_x;
    float row_total_w;
};

// `scale` multiplies the current font's metrics — match what
// SetWindowFontScale(scale) will render inside the grid's child window.
// Pass 1.0f to preserve legacy behavior.
HexLayout ComputeHexLayout(float avail_w, float scale = 1.0f);

void RenderHexHeader(const theme::Palette& pal, const HexLayout& L);
void RenderHexGrid  (GuiState& s, DocumentState& doc,
                     const theme::Palette& pal,
                     HexEditorCore& core, const HexLayout& L);

} // namespace ui
