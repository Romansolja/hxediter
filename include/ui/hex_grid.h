#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"
#include "ui/theme.h"

#include <vector>

namespace ui {

// Column x-coords shared by header and body so they pixel-align.
struct HexLayout {
    int   bytes_per_line;
    float char_w;
    float byte_w;
    float offset_w;
    std::vector<float> byte_x;
    float ascii_x;
    float row_total_w;
};

// Pass the same `scale` that the grid child uses with SetWindowFontScale(scale).
HexLayout ComputeHexLayout(float avail_w, float scale = 1.0f);

void RenderHexHeader(const theme::Palette& pal, const HexLayout& L);
void RenderHexGrid  (GuiState& s, DocumentState& doc,
                     const theme::Palette& pal,
                     HexEditorCore& core, const HexLayout& L);

}
