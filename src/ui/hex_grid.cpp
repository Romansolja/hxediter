#include "ui/hex_grid.h"
#include "ui/actions.h"
#include "ui/help_panel.h"
#include "ui/layout.h"

#include "imgui.h"

#include <algorithm>
#include <cinttypes>
#include <climits>
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

    /* Step from very wide down. 4K / ultrawide can comfortably fit
     * 128–256 bytes/row; the old cap of 64 left half the screen blank. */
    int best = 1;
    for (int candidate = 256; candidate >= 8; candidate -= 4) {
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

void RenderHexGrid(GuiState& s, DocumentState& doc,
                   const theme::Palette& pal,
                   HexEditorCore& core, const HexLayout& L) {
    const int64_t file_size = core.GetFileSize();
    const int64_t bpl       = (int64_t)L.bytes_per_line;
    const bool    readonly  = core.IsReadOnly();

    /* Total rows for the whole file. Clipper takes int — clamp at INT_MAX
     * (covers ~32 GB at 16 BPL; beyond that we'd need a 64-bit clipper). */
    int64_t total_rows64 = (file_size + bpl - 1) / bpl;
    if (total_rows64 < 0) total_rows64 = 0;
    int total_rows = (total_rows64 > (int64_t)INT_MAX)
                       ? INT_MAX
                       : (int)total_rows64;

    const float line_h = ImGui::GetTextLineHeightWithSpacing();

    /* Apply pending scroll *before* the clipper measures so the visible
     * range uses the new ScrollY. Only act if the target row isn't already
     * fully in view — otherwise arrow keys jerk-recenter the view on every
     * keystroke. PgUp/PgDn move far enough that the target is always
     * off-screen, so they re-center; arrow nav within the visible area
     * leaves scroll alone. */
    if (doc.pending_scroll_offset >= 0 && bpl > 0) {
        const int64_t target_row    = doc.pending_scroll_offset / bpl;
        const float   target_y      = (float)target_row * line_h;
        const float   row_bottom    = target_y + line_h;
        const float   current_top   = ImGui::GetScrollY();
        const float   window_h      = ImGui::GetWindowHeight();
        const float   current_bot   = current_top + window_h;
        const bool    already_in_view =
            (target_y >= current_top) && (row_bottom <= current_bot);
        if (!already_in_view) {
            float scroll_y = target_y - window_h * 0.3f;
            if (scroll_y < 0.0f) scroll_y = 0.0f;
            ImGui::SetScrollY(scroll_y);
        }
        doc.pending_scroll_offset = -1;
    }

    const ImU32 zebra_col = ImGui::GetColorU32(pal.zebra);
    const ImU32 hit_col   = ImGui::GetColorU32(pal.search_hit);

    ImGuiListClipper clipper;
    clipper.Begin(total_rows, line_h);
    while (clipper.Step()) {
        const int first_row = clipper.DisplayStart;
        const int last_row  = clipper.DisplayEnd;   /* exclusive */
        if (first_row >= last_row) continue;

        /* Pull every visible row's bytes in one fread. ~30 visible rows ×
         * 64 bytes/row = 1.9 KB; the disk cache absorbs it instantly. */
        const int64_t batch_offset = (int64_t)first_row * bpl;
        const int64_t batch_wanted = (int64_t)(last_row - first_row) * bpl;
        std::vector<unsigned char> batch =
            core.ReadAt(batch_offset, (size_t)batch_wanted);
        const int64_t batch_size = (int64_t)batch.size();

        for (int row = first_row; row < last_row; ++row) {
            const int64_t row_off = (int64_t)row * bpl;
            if (row_off >= file_size) break;
            const int64_t in_batch = row_off - batch_offset;

            ImVec2 row_p0 = ImGui::GetCursorScreenPos();
            if ((row & 1) == 1) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    row_p0,
                    ImVec2(row_p0.x + ImGui::GetWindowWidth(), row_p0.y + line_h),
                    zebra_col);
            }

            ImGui::TextDisabled("%08" PRIX64, (uint64_t)row_off);

            for (int c = 0; c < L.bytes_per_line; ++c) {
                const int64_t idx_in_batch = in_batch + (int64_t)c;
                if (idx_in_batch >= batch_size) break;
                const int64_t off = row_off + (int64_t)c;
                unsigned char b   = batch[(size_t)idx_in_batch];

                ImGui::SameLine(L.byte_x[c]);
                /* PushID(const void*) hashes the full 64-bit offset on a
                 * 64-bit build, so bytes 0x00000000 and 0x80000000 don't
                 * share an id even if the clipper ever ranges over them
                 * in the same frame (current code can't, but a future
                 * minimap could). */
                ImGui::PushID(reinterpret_cast<const void*>(
                    static_cast<uintptr_t>(off)));

                if (doc.selected_byte == off) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(0.0f, 0.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    ImGui::SetNextItemWidth(L.byte_w);
                    ImGuiInputTextFlags flags =
                        ImGuiInputTextFlags_CharsHexadecimal |
                        ImGuiInputTextFlags_CharsUppercase   |
                        ImGuiInputTextFlags_EnterReturnsTrue |
                        ImGuiInputTextFlags_AutoSelectAll;
                    if (doc.focus_edit) {
                        ImGui::SetKeyboardFocusHere();
                        doc.focus_edit = false;
                    }
                    if (ImGui::InputText("##edit", doc.edit_buf, sizeof(doc.edit_buf), flags)) {
                        CommitEdit(s, doc, core);
                    } else if (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) {
                        doc.selected_byte = -1;
                    }
                    ImGui::PopStyleVar(2);
                } else {
                    bool is_caret = (doc.caret_byte == off);
                    bool is_hit   = (doc.last_hit   == off);
                    if (is_caret || is_hit) {
                        float pulse = 1.0f;
                        if (is_caret && doc.selected_byte >= 0) {
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
                        /* Match the clipper's row height so the caret/hit
                         * background covers the full row band, leaving no
                         * sliver of zebra peeking through. */
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            ImVec2(cp.x - 1, cp.y),
                            ImVec2(cp.x + L.byte_w + 1, cp.y + line_h),
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
                        doc.caret_byte  = off;
                        doc.focus_field = GuiState::FOCUS_BYTE;
                        s.MarkInteracted();
                        if (!readonly) {
                            doc.selected_byte = off;
                            std::snprintf(doc.edit_buf, sizeof(doc.edit_buf), "%02X", b);
                            doc.focus_edit = true;
                        } else {
                            s.SetStatus("File is read-only", GuiState::STATUS_WARN);
                        }
                    }
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }

            ImGui::SameLine(L.ascii_x);
            /* Sized for the worst case the layout loop can produce. */
            char ascii_buf[257];
            int  ascii_len = 0;
            const int ascii_cap = (int)sizeof(ascii_buf) - 1;
            for (int c = 0; c < L.bytes_per_line; ++c) {
                const int64_t idx_in_batch = in_batch + (int64_t)c;
                if (idx_in_batch >= batch_size) break;
                unsigned char b = batch[(size_t)idx_in_batch];
                if (ascii_len < ascii_cap)
                    ascii_buf[ascii_len++] = (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
            }
            ascii_buf[ascii_len] = '\0';
            ImGui::TextUnformatted(ascii_buf);
        }
    }
    clipper.End();

    if (s.help_anim > 0.01f) {
        RenderHelpPanel(s, pal, s.help_anim);
    }
}

} /* namespace ui */
