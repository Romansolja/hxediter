/* hex_grid.h — Responsive hex byte grid (layout, header, body). */

#pragma once

#include "hex_editor_core.h"
#include "ui/gui_state.h"
#include "ui/theme.h"

#include <vector>

namespace ui {

/* Precomputed column x-coordinates so the header strip and body rows
 * land on the exact same pixels. All values are relative to the line
 * start in the current ImGui window. */
struct HexLayout {
    int   bytes_per_line;
    float char_w;
    float byte_w;
    float offset_w;
    std::vector<float> byte_x;
    float ascii_x;
    float row_total_w;
};

HexLayout ComputeHexLayout(float avail_w);

void RenderHexHeader(const theme::Palette& pal, const HexLayout& L);
void RenderHexGrid  (GuiState& s, const theme::Palette& pal,
                     HexEditorCore& core, const HexLayout& L);

} /* namespace ui */
