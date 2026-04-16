/* status_bar.cpp — Bottom status bar with mode/state badges, transient
 * messages, and the contextual hint that follows keyboard focus. */

#include "ui/status_bar.h"
#include "ui/layout.h"

#include "imgui.h"

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
    /* Lift the badge up so its text baseline matches plain Text() that
     * was aligned with AlignTextToFramePadding earlier in the line. */
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

void RenderStatusBar(GuiState& s, const theme::Palette& pal, HexEditorCore& core) {
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();

    ImGui::Text("0x%08" PRIX64 "   Page %" PRId64 "/%" PRId64 "   %" PRId64 " B",
                (uint64_t)core.GetCurrentOffset(),
                core.GetPageNumber(),
                core.GetTotalPages(),
                core.GetFileSize());

    /* Mode / state badges */
    ImGui::SameLine(0, 14);
    Badge("OVR", pal.status_neutral_bg, pal.status_neutral_fg);

    ImGui::SameLine(0, 6);
    if (core.IsReadOnly()) Badge("READ-ONLY", pal.status_read_bg,    pal.status_read_fg);
    else                   Badge("EDIT",      pal.status_neutral_bg, pal.status_neutral_fg);

    ImGui::SameLine(0, 6);
    int undos = core.GetUndoCount();
    if (undos > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "MODIFIED %d", undos);
        Badge(buf, pal.status_warn_bg, pal.status_warn_fg);
    } else {
        Badge("CLEAN", pal.status_ok_bg, pal.status_ok_fg);
    }

    /* Help marker explaining the three mode/state badges. */
    ImGui::SameLine(0, 6);
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

    /* Transient status badge (last action / error). */
    if (s.status_timer > 0.0f) {
        ImVec4 bg, fg;
        switch (s.status_kind) {
            case GuiState::STATUS_OK:    bg = pal.status_ok_bg;      fg = pal.status_ok_fg;      break;
            case GuiState::STATUS_WARN:  bg = pal.status_warn_bg;    fg = pal.status_warn_fg;    break;
            case GuiState::STATUS_ERROR: bg = pal.status_err_bg;     fg = pal.status_err_fg;     break;
            default:                     bg = pal.status_neutral_bg; fg = pal.status_neutral_fg; break;
        }
        const float fade_window = 0.5f;
        float alpha_raw = s.status_timer / fade_window;
        if (alpha_raw < 0.0f) alpha_raw = 0.0f;
        if (alpha_raw > 1.0f) alpha_raw = 1.0f;
        float alpha = (s.status_timer >= fade_window) ? 1.0f : alpha_raw;
        ImGui::SameLine(0, 14);
        Badge(s.status_msg.c_str(), bg, fg, alpha);
        s.status_timer -= ImGui::GetIO().DeltaTime;
    }

    /* Contextual hint, dim, end-of-line. */
    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("%s", GetContextualHint(s, core));
}

} /* namespace ui */
