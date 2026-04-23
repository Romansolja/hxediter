#include "ui/status_bar.h"
#include "ui/layout.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

namespace ui {

void Badge(const char* text, ImVec4 bg, ImVec4 fg, float alpha) {
    bg.w *= alpha;
    fg.w *= alpha;
    ImVec2 ts      = ImGui::CalcTextSize(text);
    ImVec2 padding = ImVec2(layout::kBadgePadX, layout::kBadgePadY);
    ImVec2 p0      = ImGui::GetCursorScreenPos();
    /* Baseline-align with AlignTextToFramePadding text on the same line. */
    float frame_pad_y = ImGui::GetStyle().FramePadding.y;
    p0.y += frame_pad_y - padding.y;

    ImVec2 size(ts.x + padding.x * 2, ts.y + padding.y * 2);
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), layout::kBadgeRounding);
    dl->AddRect(p0, p1,
                ImGui::GetColorU32(ImVec4(fg.x, fg.y, fg.z, 0.35f * alpha)),
                layout::kBadgeRounding, 0, 1.0f);
    dl->AddText(ImVec2(p0.x + padding.x, p0.y + padding.y),
                ImGui::GetColorU32(fg), text);

    ImGui::Dummy(size);
}

const char* GetContextualHint(const GuiState& s, const HexEditorCore& core) {
    if (s.selected_byte >= 0)
        return "Type 1-2 hex digits, Enter to commit, Esc to cancel";
    if (s.focus_field == GuiState::FOCUS_GOTO)
        return "Hex offset (e.g. 1A0), Enter to jump";
    if (s.focus_field == GuiState::FOCUS_SEARCH)
        return "Hex bytes (e.g. DE AD BE EF), Enter to find";
    if (core.IsReadOnly())
        return "Read-only file: click bytes to inspect, F1 for shortcuts";
    return "Click any byte to edit, arrow keys to move, F1 for shortcuts";
}

/* Matches help_panel.cpp's close affordance so both sites read identically. */
static bool DismissButton(const char* id, float size,
                          const theme::Palette& pal, float alpha) {
    ImVec4 hover_c = pal.help_close_hover;  hover_c.w *= alpha;
    ImVec4 act_c   = pal.help_close_active; act_c.w   *= alpha;
    ImVec4 glyph   = pal.help_close_glyph;  glyph.w   *= alpha;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float line_center = pos.y + ImGui::GetStyle().FramePadding.y
                      + ImGui::GetTextLineHeight() * 0.5f;
    pos.y = line_center - size * 0.5f;
    ImGui::SetCursorScreenPos(pos);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover_c);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act_c);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameBorderSize, 0.0f);
    char btn_id[48];
    std::snprintf(btn_id, sizeof(btn_id), "##%s", id);
    bool clicked = ImGui::Button(btn_id, ImVec2(size, size));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImU32       col = ImGui::GetColorU32(glyph);
    float       pad = size * (5.0f / 18.0f);
    dl->AddLine(ImVec2(pos.x + pad,        pos.y + pad),
                ImVec2(pos.x + size - pad, pos.y + size - pad), col, 1.5f);
    dl->AddLine(ImVec2(pos.x + size - pad, pos.y + pad),
                ImVec2(pos.x + pad,        pos.y + size - pad), col, 1.5f);

    return clicked;
}

void RenderStatusBar(GuiState& s, const theme::Palette& pal, HexEditorCore& core) {
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();

    ImGui::Text("0x%08" PRIX64 "   Page %" PRId64 "/%" PRId64 "   %" PRId64 " B",
                (uint64_t)core.GetCurrentOffset(),
                core.GetPageNumber(),
                core.GetTotalPages(),
                core.GetFileSize());

    ImGui::SameLine(0, layout::kStatusGroupGap);
    Badge("OVR", pal.status_neutral_bg, pal.status_neutral_fg);

    ImGui::SameLine(0, layout::kStatusInGroup);
    if (core.IsReadOnly()) Badge("READ-ONLY", pal.status_read_bg,    pal.status_read_fg);
    else                   Badge("EDIT",      pal.status_neutral_bg, pal.status_neutral_fg);

    ImGui::SameLine(0, layout::kStatusInGroup);
    int undos = core.GetUndoCount();
    if (undos > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "MODIFIED %d", undos);
        Badge(buf, pal.status_warn_bg, pal.status_warn_fg);
    } else {
        Badge("CLEAN", pal.status_ok_bg, pal.status_ok_fg);
    }

    if (s.externally_modified) {
        ImGui::SameLine(0, layout::kStatusInGroup);
        Badge("EXTERNALLY MODIFIED", pal.status_err_bg, pal.status_err_fg);
    }

    ImGui::SameLine(0, layout::kStatusInGroup);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted("Status badges");
        ImGui::Separator();
        ImGui::TextUnformatted(
            "OVR        Overwrite mode. Edits replace the byte under "
            "the caret in place; the file size never changes. (Insert "
            "mode is not currently supported.)");
        ImGui::Spacing();
        ImGui::TextUnformatted(
            "EDIT       The file was opened read/write. Click any byte "
            "to edit it. Shown as READ-ONLY (yellow) when the file "
            "could only be opened for reading.");
        ImGui::Spacing();
        ImGui::TextUnformatted(
            "CLEAN      No edits have been made in this session. After "
            "the first edit this becomes MODIFIED N, where N is the "
            "number of undo steps still available (Ctrl+Z). Note that "
            "edits are written to disk immediately.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    /* Sticky messages pin until the user clicks x; non-sticky fade on a timer. */
    if (s.status_timer > 0.0f) {
        ImVec4 bg, fg;
        switch (s.status_kind) {
            case GuiState::STATUS_OK:    bg = pal.status_ok_bg;      fg = pal.status_ok_fg;      break;
            case GuiState::STATUS_WARN:  bg = pal.status_warn_bg;    fg = pal.status_warn_fg;    break;
            case GuiState::STATUS_ERROR: bg = pal.status_err_bg;     fg = pal.status_err_fg;     break;
            default:                     bg = pal.status_neutral_bg; fg = pal.status_neutral_fg; break;
        }
        float alpha = 1.0f;
        if (!s.status_sticky) {
            const float fade_window = 0.5f;
            float alpha_raw = s.status_timer / fade_window;
            if (alpha_raw < 0.0f) alpha_raw = 0.0f;
            if (alpha_raw > 1.0f) alpha_raw = 1.0f;
            alpha = (s.status_timer >= fade_window) ? 1.0f : alpha_raw;
        }
        ImGui::SameLine(0, layout::kStatusGroupGap);
        Badge(s.status_msg.c_str(), bg, fg, alpha);

        if (s.status_sticky) {
            ImGui::SameLine(0, 4.0f);
            float sz = ImGui::GetFrameHeight() - 4.0f;
            if (sz < 10.0f) sz = 10.0f;
            if (DismissButton("dismiss_status", sz, pal, alpha)) {
                s.status_msg.clear();
                s.status_timer  = 0.0f;
                s.status_sticky = false;
            }
        } else {
            s.status_timer -= ImGui::GetIO().DeltaTime;
        }
    }

    /* Priority tail: sticky (above) > startup metric > contextual hint.
     * Hint truncates with ellipsis then drops when <~3 chars fit. */
    char metric[32];
    if (s.startup_measured)
        std::snprintf(metric, sizeof(metric), "Startup: %.0f ms", s.startup_duration_ms);
    else
        std::snprintf(metric, sizeof(metric), "Startup: \xE2\x80\xA6");

    const char* hint      = GetContextualHint(s, core);
    float       metric_w  = ImGui::CalcTextSize(metric).x;
    float       hint_full = ImGui::CalcTextSize(hint).x;
    float       char_w    = ImGui::CalcTextSize("A").x;

    ImGui::SameLine(0, 0);
    float line_start_x = ImGui::GetCursorPosX();
    float avail        = ImGui::GetContentRegionAvail().x;

    bool  show_metric = (avail >= metric_w + layout::kStatusGutter);
    float hint_room   = show_metric ? (avail - metric_w - layout::kStatusGutter) : avail;
    bool  show_hint   = (hint_room >= layout::kStatusGroupGap + 3.0f * char_w);
    float hint_budget = 0.0f;
    if (show_hint) {
        hint_budget = hint_room - layout::kStatusGroupGap;
        if (hint_budget > hint_full) hint_budget = hint_full;
    }

    if (show_hint) {
        ImGui::SameLine(0, layout::kStatusGroupGap);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        pos.y += ImGui::GetStyle().FramePadding.y;
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::RenderTextEllipsis(
            ImGui::GetWindowDrawList(), pos,
            ImVec2(pos.x + hint_budget, pos.y + ImGui::GetTextLineHeight()),
            pos.x + hint_budget, pos.x + hint_budget,
            hint, nullptr, nullptr);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(hint_budget, ImGui::GetTextLineHeight()));
    }

    if (show_metric) {
        float metric_abs_x = line_start_x + avail - metric_w;
        ImGui::SameLine(metric_abs_x);
        ImGui::TextDisabled("%s", metric);
    }
}

} /* namespace ui */
