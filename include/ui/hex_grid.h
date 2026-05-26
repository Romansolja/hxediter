#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"
#include "ui/theme.h"

#include <cstdint>
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
// `file_size` lets the layout bump bytes-per-line for files past INT_MAX rows at
// the width-chosen BPL — see MinBytesPerLineForAddressability in hex_grid_math.h.
HexLayout ComputeHexLayout(float avail_w, int64_t file_size, float scale = 1.0f);

void RenderHexHeader(const theme::Palette& pal, const HexLayout& L);
void RenderHexGrid  (GuiState& s, DocumentState& doc,
                     const theme::Palette& pal,
                     HexEditorCore& core, const HexLayout& L);

}
