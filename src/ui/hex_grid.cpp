#include "ui/hex_grid.h"
#include "ui/actions.h"
#include "ui/help_panel.h"
#include "ui/layout.h"

#include "imgui.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ui {

static float ComputeHexRowWidth(float offset_w, float char_w, float byte_w,
                                int bytes_per_line) {
    const float gap_byte  = char_w * layout::kGapByteMul;
    const float gap_quad  = char_w * layout::kGapQuadMul;
    const float gap_octet = char_w * layout::kGapOctetMul;
    float x = offset_w + char_w * layout::kOffsetGapMul;
    for (int i = 0; i < bytes_per_line; ++i) {
        x += byte_w;
        if (i == bytes_per_line - 1) continue;
        x += gap_byte;
        if (((i + 1) % 4) == 0) x += gap_quad;
        if (((i + 1) % 8) == 0) x += gap_octet;
    }
    float ascii_x = x + char_w * layout::kAsciiGapMul;
    return ascii_x + char_w * (bytes_per_line + 1);
}

HexLayout ComputeHexLayout(float avail_w, float scale) {
    HexLayout L;
    L.bytes_per_line = 16;
    /* CalcTextSize returns unscaled metrics (outer window's FontWindowScale
     * is 1); multiply by scale so the layout matches what the grid child
     * will actually render at SetWindowFontScale(scale). */
    L.char_w   = ImGui::CalcTextSize("0").x * scale;
    L.byte_w   = ImGui::CalcTextSize("FF").x * scale;
    L.offset_w = ImGui::CalcTextSize("00000000").x * scale;

    int best = 1;
    for (int candidate = 64; candidate >= 8; candidate -= 4) {
        if (ComputeHexRowWidth(L.offset_w, L.char_w, L.byte_w, candidate) <= avail_w) {
            best = candidate;
            break;
        }
    }
    if (best == 1) {
        for (int candidate = 4; candidate >= 1; --candidate) {
            if (ComputeHexRowWidth(L.offset_w, L.char_w, L.byte_w, candidate) <= avail_w) {
                best = candidate;
                break;
            }
        }
    }
    L.bytes_per_line = best;
    L.byte_x.resize((size_t)L.bytes_per_line);

    const float gap_byte  = L.char_w * layout::kGapByteMul;
    const float gap_quad  = L.char_w * layout::kGapQuadMul;
    const float gap_octet = L.char_w * layout::kGapOctetMul;

    float x = L.offset_w + L.char_w * layout::kOffsetGapMul;
    for (int i = 0; i < L.bytes_per_line; ++i) {
        L.byte_x[i] = x;
        x += L.byte_w;
        if (i == L.bytes_per_line - 1) continue;
        x += gap_byte;
        if (((i + 1) % 4) == 0) x += gap_quad;
        if (((i + 1) % 8) == 0) x += gap_octet;
    }
    L.ascii_x     = x + L.char_w * layout::kAsciiGapMul;
    L.row_total_w = L.ascii_x + L.char_w * (L.bytes_per_line + 1);
    return L;
}

void RenderHexHeader(const theme::Palette& pal, const HexLayout& L) {
    ImVec2 p0    = ImGui::GetCursorScreenPos();
    float  row_h = ImGui::GetTextLineHeight() + layout::kHeaderExtraH;
    float  w     = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        p0,
        ImVec2(p0.x + w, p0.y + row_h),
        ImGui::GetColorU32(pal.header_bg));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p0.x,     p0.y + row_h - 1),
        ImVec2(p0.x + w, p0.y + row_h - 1),
        ImGui::GetColorU32(pal.header_border));

    ImGui::Dummy(ImVec2(0, 4));

    ImGui::PushStyleColor(ImGuiCol_Text, pal.header_text);
    ImGui::TextUnformatted("Offset");
    for (int c = 0; c < L.bytes_per_line; ++c) {
        ImGui::SameLine(L.byte_x[c]);
        ImGui::Text("%02X", c);
    }
    ImGui::SameLine(L.ascii_x);
    ImGui::TextUnformatted("ASCII");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 4));
}

void RenderHexGrid(GuiState& s, const theme::Palette& pal,
                   HexEditorCore& core, const HexLayout& L) {
    auto    page       = core.GetPageData();
    int64_t base       = core.GetCurrentOffset();
    bool    readonly   = core.IsReadOnly();
    size_t  byte_count = page.size();

    const ImU32 zebra_col = ImGui::GetColorU32(pal.zebra);
    const ImU32 hit_col   = ImGui::GetColorU32(pal.search_hit);

    int lines = (int)((byte_count + (size_t)L.bytes_per_line - 1) / (size_t)L.bytes_per_line);
    for (int line = 0; line < lines; ++line) {
        size_t  line_start = (size_t)line * (size_t)L.bytes_per_line;
        if (line_start >= byte_count) break;
        int64_t line_off   = base + (int64_t)line_start;

        ImVec2 row_p0 = ImGui::GetCursorScreenPos();
        float  row_h  = ImGui::GetTextLineHeightWithSpacing();
        if ((line & 1) == 1) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                row_p0,
                ImVec2(row_p0.x + ImGui::GetWindowWidth(), row_p0.y + row_h),
                zebra_col);
        }

        ImGui::TextDisabled("%08" PRIX64, (uint64_t)line_off);

        for (int c = 0; c < L.bytes_per_line; ++c) {
            size_t  idx = line_start + (size_t)c;
            if (idx >= byte_count) break;
            int64_t off = base + (int64_t)idx;
            unsigned char b = page[idx];

            ImGui::SameLine(L.byte_x[c]);
            ImGui::PushID((int)idx + line * 64);

            if (s.selected_byte == off) {
                /* Zero padding + explicit width to fit one byte cell. */
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                ImGui::SetNextItemWidth(L.byte_w);
                ImGuiInputTextFlags flags =
                    ImGuiInputTextFlags_CharsHexadecimal |
                    ImGuiInputTextFlags_CharsUppercase   |
                    ImGuiInputTextFlags_EnterReturnsTrue |
                    ImGuiInputTextFlags_AutoSelectAll;
                if (s.focus_edit) {
                    ImGui::SetKeyboardFocusHere();
                    s.focus_edit = false;
                }
                if (ImGui::InputText("##edit", s.edit_buf, sizeof(s.edit_buf), flags)) {
                    CommitEdit(s, core);
                } else if (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) {
                    s.selected_byte = -1;
                }
                ImGui::PopStyleVar(2);
            } else {
                /* Draw highlight before text so the glyph stays readable. */
                bool is_caret = (s.caret_byte == off);
                bool is_hit   = (s.last_hit   == off);
                if (is_caret || is_hit) {
                    float pulse = 1.0f;
                    if (is_caret && s.selected_byte >= 0) {
                        pulse = 0.72f + 0.28f *
                            (std::sin((float)ImGui::GetTime() * layout::kCaretPulseHz) * 0.5f + 0.5f);
                    }
                    ImU32 bg_col;
                    if (is_hit) {
                        bg_col = hit_col;
                    } else {
                        ImVec4 caret_bg_col = pal.caret_bg;
                        caret_bg_col.w *= pulse;
                        bg_col = ImGui::GetColorU32(caret_bg_col);
                    }
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(cp.x - 1, cp.y),
                        ImVec2(cp.x + L.byte_w + 1, cp.y + ImGui::GetTextLineHeight() + 2),
                        bg_col,
                        layout::kCaretRounding);
                }

                ImVec4 col = theme::ColorForByte(pal, b);
                if (is_caret) col = pal.caret_text;
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                char label[8];
                std::snprintf(label, sizeof(label), "%02X", b);
                if (ImGui::Selectable(label,
                                      false,
                                      ImGuiSelectableFlags_AllowDoubleClick |
                                      ImGuiSelectableFlags_DontClosePopups,
                                      ImVec2(L.byte_w, 0))) {
                    s.caret_byte  = off;
                    s.focus_field = GuiState::FOCUS_BYTE;
                    s.MarkInteracted();
                    if (!readonly) {
                        s.selected_byte = off;
                        std::snprintf(s.edit_buf, sizeof(s.edit_buf), "%02X", b);
                        s.focus_edit = true;
                    } else {
                        s.SetStatus("File is read-only", GuiState::STATUS_WARN);
                    }
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }

        ImGui::SameLine(L.ascii_x);
        char ascii_buf[65];
        int  ascii_len = 0;
        for (int c = 0; c < L.bytes_per_line; ++c) {
            size_t idx = line_start + (size_t)c;
            if (idx >= byte_count) break;
            unsigned char b = page[idx];
            if (ascii_len < 64)
                ascii_buf[ascii_len++] = (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
        }
        ascii_buf[ascii_len] = '\0';
        ImGui::TextUnformatted(ascii_buf);
    }

    if (s.help_anim > 0.01f) {
        RenderHelpPanel(s, pal, s.help_anim);
    }
}

} /* namespace ui */
