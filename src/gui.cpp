/* gui.cpp — Dear ImGui hex editor view */

#include "gui.h"
#include "platform.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include <inttypes.h>
#include <cmath>

namespace {

/* ------------------------------------------------------------------ */
/* Persistent UI state                                                */
/* ------------------------------------------------------------------ */
char     g_goto_buf[17]   = "";
char     g_search_buf[64] = "";
int64_t  g_selected_byte  = -1;   /* offset of byte currently being edited, or -1 */
int64_t  g_caret_byte     = -1;   /* offset of focused/active byte (persists past edit) */
char     g_edit_buf[3]    = "";   /* in-progress hex edit text */
bool     g_focus_edit     = false;
int64_t  g_last_hit       = -1;   /* offset of the last search hit (for highlight) */

/* Status bar / contextual hint state */
enum StatusKind { STATUS_INFO, STATUS_OK, STATUS_WARN, STATUS_ERROR };
enum FocusField { FOCUS_NONE, FOCUS_GOTO, FOCUS_SEARCH, FOCUS_BYTE };

std::string g_status_msg;
float       g_status_timer = 0.0f;
StatusKind  g_status_kind  = STATUS_INFO;
FocusField  g_focus_field  = FOCUS_NONE;

/* Help / onboarding panel */
bool g_show_help        = true;   /* visible until user starts working */
bool g_user_interacted  = false;  /* sticky once user has done anything */
float g_help_anim       = 1.0f;   /* 0..1 smooth visibility */
ImFont* g_ui_font       = nullptr;
ImFont* g_mono_font     = nullptr;

void SetStatus(const std::string& msg, StatusKind kind = STATUS_INFO) {
    g_status_msg   = msg;
    g_status_kind  = kind;
    g_status_timer = 3.5f;
}

void MarkInteracted() {
    g_user_interacted = true;
    g_show_help       = false;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
bool ParseHexU64(const char* s, uint64_t* out) {
    if (!s || !*s) return false;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return false;
    uint64_t v = 0;
    while (*s) {
        char c = *s++;
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c == ' ' || c == '\t') continue;
        else return false;
        v = (v << 4) | (uint64_t)d;
    }
    *out = v;
    return true;
}

std::vector<unsigned char> ParseHexBytes(const char* s) {
    std::vector<unsigned char> out;
    if (!s) return out;
    int hi = -1;
    while (*s) {
        char c = *s++;
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c == ' ' || c == '\t' || c == ',') continue;
        else { out.clear(); return out; }
        if (hi < 0) {
            hi = d;
        } else {
            out.push_back((unsigned char)((hi << 4) | d));
            hi = -1;
        }
    }
    if (hi >= 0) { out.clear(); }
    return out;
}

ImVec4 ColorForByte(unsigned char b) {
    if (b == 0x00) return ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    if (b >= 0x20 && b <= 0x7E) return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
}

/* ------------------------------------------------------------------ */
/* Action handlers                                                    */
/* ------------------------------------------------------------------ */
void DoGoto(HexEditorCore& core) {
    uint64_t off;
    if (!ParseHexU64(g_goto_buf, &off)) {
        SetStatus("Invalid offset", STATUS_ERROR);
        return;
    }
    if (!core.GoToOffset((int64_t)off)) {
        SetStatus("Offset out of range", STATUS_ERROR);
        return;
    }
    MarkInteracted();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Jumped to 0x%" PRIX64, (uint64_t)off);
    SetStatus(buf, STATUS_OK);
}

void DoSearch(HexEditorCore& core) {
    auto pat = ParseHexBytes(g_search_buf);
    if (pat.empty()) {
        SetStatus("Invalid hex pattern", STATUS_ERROR);
        return;
    }
    auto res = core.Search(pat);
    if (!res) {
        g_last_hit = -1;
        SetStatus("Pattern not found", STATUS_WARN);
        return;
    }
    MarkInteracted();
    g_last_hit = res->offset;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Found at 0x%" PRIX64, (uint64_t)res->offset);
    SetStatus(buf, STATUS_OK);
}

void DoUndo(HexEditorCore& core) {
    auto res = core.Undo();
    if (!res) {
        SetStatus("Nothing to undo", STATUS_WARN);
        return;
    }
    MarkInteracted();
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "Undid 0x%" PRIX64 ": 0x%02X -> 0x%02X (%d left)",
                  (uint64_t)res->offset, res->undone_val, res->restored_val,
                  res->remaining_undos);
    SetStatus(buf, STATUS_OK);
}

void CommitEdit(HexEditorCore& core) {
    if (g_selected_byte < 0) return;
    if (g_edit_buf[0] == '\0') { g_selected_byte = -1; return; }

    unsigned int v = 0;
    if (std::sscanf(g_edit_buf, "%x", &v) != 1 || v > 0xFF) {
        SetStatus("Invalid byte value", STATUS_ERROR);
        g_selected_byte = -1;
        return;
    }
    auto res = core.EditByte(g_selected_byte, (unsigned char)v);
    if (!res) {
        SetStatus("Edit failed (read-only?)", STATUS_ERROR);
    } else {
        MarkInteracted();
        char buf[80];
        std::snprintf(buf, sizeof(buf),
                      "Edited 0x%" PRIX64 ": 0x%02X -> 0x%02X",
                      (uint64_t)res->offset, res->old_val, res->new_val);
        SetStatus(buf, STATUS_OK);
    }
    g_selected_byte = -1;
}

/* ------------------------------------------------------------------ */
/* Hex grid layout                                                    */
/* ------------------------------------------------------------------ */
/* Precomputed column positions so the header strip and every body    */
/* row land on the exact same x coordinates. All values are relative  */
/* to the start of the line in the current ImGui window.              */
struct HexLayout {
    int bytes_per_line;
    float char_w;
    float byte_w;
    float offset_w;
    std::vector<float> byte_x;
    float ascii_x;
    float row_total_w;
};

float ComputeHexRowWidth(float offset_w, float char_w, float byte_w, int bytes_per_line) {
    const float gap_byte  = char_w * 0.7f;
    const float gap_quad  = char_w * 0.7f;
    const float gap_octet = char_w * 1.2f;
    float x = offset_w + char_w * 2.0f;
    for (int i = 0; i < bytes_per_line; ++i) {
        x += byte_w;
        if (i == bytes_per_line - 1) continue;
        x += gap_byte;
        if (((i + 1) % 4) == 0) x += gap_quad;
        if (((i + 1) % 8) == 0) x += gap_octet;
    }
    float ascii_x = x + char_w * 2.0f;
    return ascii_x + char_w * (bytes_per_line + 1);
}

HexLayout ComputeHexLayout(float avail_w) {
    HexLayout L;
    L.bytes_per_line = 16;
    L.char_w   = ImGui::CalcTextSize("0").x;
    L.byte_w   = ImGui::CalcTextSize("FF").x;
    L.offset_w = ImGui::CalcTextSize("00000000").x;

    /* Responsive bytes-per-line target (snap to multiples of 4 when possible). */
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

    /* Three tiers of horizontal gap so the eye can latch onto 4- and  *
     * 8-byte groups without explicit separator glyphs.                */
    const float gap_byte  = L.char_w * 0.7f;   /* between every byte    */
    const float gap_quad  = L.char_w * 0.7f;   /* extra at every 4 bytes*/
    const float gap_octet = L.char_w * 1.2f;   /* extra at every 8 bytes*/

    float x = L.offset_w + L.char_w * 2.0f; /* gap after offset column */
    for (int i = 0; i < L.bytes_per_line; ++i) {
        L.byte_x[i] = x;
        x += L.byte_w;
        if (i == L.bytes_per_line - 1) continue;
        x += gap_byte;
        if (((i + 1) % 4) == 0) x += gap_quad;
        if (((i + 1) % 8) == 0) x += gap_octet;
    }
    L.ascii_x     = x + L.char_w * 2.0f;
    L.row_total_w = L.ascii_x + L.char_w * (L.bytes_per_line + 1);
    return L;
}

/* ------------------------------------------------------------------ */
/* Header strip                                                       */
/* ------------------------------------------------------------------ */
void RenderHexHeader(const HexLayout& L) {
    /* Filled background strip that runs the full window width.       */
    ImVec2 p0    = ImGui::GetCursorScreenPos();
    float  row_h = ImGui::GetTextLineHeight() + 8.0f;
    float  w     = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        p0,
        ImVec2(p0.x + w, p0.y + row_h),
        ImGui::GetColorU32(ImVec4(0.16f, 0.18f, 0.22f, 1.00f)));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p0.x,     p0.y + row_h - 1),
        ImVec2(p0.x + w, p0.y + row_h - 1),
        ImGui::GetColorU32(ImVec4(0.30f, 0.34f, 0.42f, 1.00f)));

    ImGui::Dummy(ImVec2(0, 4));

    const ImVec4 hdr_text(0.95f, 0.97f, 1.00f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_Text, hdr_text);

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

/* Forward declaration: the help panel is defined further down with   *
 * the status-bar helpers but is rendered from inside the grid body.   */
void RenderHelpPanel(float visibility);

/* ------------------------------------------------------------------ */
/* Hex grid body                                                      */
/* ------------------------------------------------------------------ */
void RenderHexGrid(HexEditorCore& core, const HexLayout& L) {
    auto    page       = core.GetPageData();
    int64_t base       = core.GetCurrentOffset();
    bool    readonly   = core.IsReadOnly();
    size_t  byte_count = page.size();

    const ImU32 zebra_col   = ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.025f));
    const ImU32 hit_col     = ImGui::GetColorU32(ImVec4(0.20f, 0.70f, 0.25f, 0.55f));

    int lines = (int)((byte_count + (size_t)L.bytes_per_line - 1) / (size_t)L.bytes_per_line);
    for (int line = 0; line < lines; ++line) {
        size_t  line_start = (size_t)line * (size_t)L.bytes_per_line;
        if (line_start >= byte_count) break;
        int64_t line_off   = base + (int64_t)line_start;

        /* Zebra row tint behind every other line. */
        ImVec2 row_p0 = ImGui::GetCursorScreenPos();
        float  row_h  = ImGui::GetTextLineHeightWithSpacing();
        if ((line & 1) == 1) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                row_p0,
                ImVec2(row_p0.x + ImGui::GetWindowWidth(), row_p0.y + row_h),
                zebra_col);
        }

        /* Offset column — dimmed so it never competes with data. */
        ImGui::TextDisabled("%08" PRIX64, (uint64_t)line_off);

        /* Hex bytes */
        for (int c = 0; c < L.bytes_per_line; ++c) {
            size_t  idx = line_start + (size_t)c;
            if (idx >= byte_count) break;
            int64_t off = base + (int64_t)idx;
            unsigned char b = page[idx];

            ImGui::SameLine(L.byte_x[c]);
            ImGui::PushID((int)idx + line * 64);

            if (g_selected_byte == off) {
                /* Inline editor */
                ImGui::SetNextItemWidth(L.byte_w + 6.0f);
                ImGuiInputTextFlags flags =
                    ImGuiInputTextFlags_CharsHexadecimal |
                    ImGuiInputTextFlags_CharsUppercase   |
                    ImGuiInputTextFlags_EnterReturnsTrue |
                    ImGuiInputTextFlags_AutoSelectAll;
                if (g_focus_edit) {
                    ImGui::SetKeyboardFocusHere();
                    g_focus_edit = false;
                }
                if (ImGui::InputText("##edit", g_edit_buf, sizeof(g_edit_buf), flags)) {
                    CommitEdit(core);
                } else if (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) {
                    /* Lost focus without Enter — cancel */
                    g_selected_byte = -1;
                }
            } else {
                /* Background highlight for caret / search hit. Drawn  *
                 * before the text so the glyph stays fully readable.  */
                bool is_caret = (g_caret_byte == off);
                bool is_hit   = (g_last_hit   == off);
                if (is_caret || is_hit) {
                    float pulse = 1.0f;
                    if (is_caret && g_selected_byte >= 0) {
                        pulse = 0.72f + 0.28f * (std::sin((float)ImGui::GetTime() * 5.0f) * 0.5f + 0.5f);
                    }
                    ImU32 bg_col = is_hit ? hit_col :
                        ImGui::GetColorU32(ImVec4(0.30f, 0.55f, 0.95f, 0.45f * pulse));
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(cp.x - 1, cp.y),
                        ImVec2(cp.x + L.byte_w + 1, cp.y + ImGui::GetTextLineHeight() + 2),
                        bg_col,
                        3.0f);
                }

                ImVec4 col = ColorForByte(b);
                if (is_caret) col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); /* brighter on focus */
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                char label[8];
                std::snprintf(label, sizeof(label), "%02X", b);
                if (ImGui::Selectable(label,
                                      false,
                                      ImGuiSelectableFlags_AllowDoubleClick |
                                      ImGuiSelectableFlags_DontClosePopups,
                                      ImVec2(L.byte_w, 0))) {
                    g_caret_byte = off;
                    g_focus_field = FOCUS_BYTE;
                    MarkInteracted();
                    if (!readonly) {
                        g_selected_byte = off;
                        std::snprintf(g_edit_buf, sizeof(g_edit_buf), "%02X", b);
                        g_focus_edit = true;
                    } else {
                        SetStatus("File is read-only", STATUS_WARN);
                    }
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }

        /* ASCII column — pinned to its own column x. */
        ImGui::SameLine(L.ascii_x);
        char ascii_buf[65];
        int  ascii_len = 0;
        for (int c = 0; c < L.bytes_per_line; ++c) {
            size_t idx = line_start + (size_t)c;
            if (idx >= byte_count) break;
            unsigned char b = page[idx];
            if (ascii_len < 64) ascii_buf[ascii_len++] = (b >= 0x20 && b <= 0x7E) ? (char)b : '.';
        }
        ascii_buf[ascii_len] = '\0';
        ImGui::TextUnformatted(ascii_buf);
    }

    /* Help panel fills leftover vertical space until the user starts  *
     * working. Rendered inside the grid child so it scrolls with it.  */
    if (g_help_anim > 0.01f) {
        RenderHelpPanel(g_help_anim);
    }
}

/* ------------------------------------------------------------------ */
/* Toolbar                                                            */
/* ------------------------------------------------------------------ */
/* Three task groups: NAV (paging), JUMP (offset entry), SEARCH/EDIT  *
 * (find + undo). Each group has one primary action styled brighter,  *
 * with extra horizontal space between groups so the eye can parse    *
 * the toolbar as three things instead of nine.                       */
void RenderToolbar(HexEditorCore& core) {
    const float group_gap = 28.0f;

    auto PushPrimary = []() {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.48f, 0.85f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.58f, 0.95f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.40f, 0.78f, 1.00f));
    };
    auto PushSecondary = []() {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.21f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.27f, 0.31f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.13f, 0.13f, 0.16f, 1.00f));
    };
    auto PopBtn = []() { ImGui::PopStyleColor(3); };

    /* Aligning text to frame padding once at the start of the line    *
     * lifts every following Text() so labels sit on the same baseline *
     * as buttons and input fields.                                    */
    ImGui::AlignTextToFramePadding();

    /* ---------------- Navigation ---------------- */
    ImGui::TextDisabled("NAV");
    ImGui::SameLine();
    PushSecondary();
    if (ImGui::Button("<< Prev")) {
        if (core.PagePrev()) MarkInteracted();
        else SetStatus("At start of file", STATUS_WARN);
    }
    PopBtn();
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Next >>")) {
        if (core.PageNext()) MarkInteracted();
        else SetStatus("At end of file", STATUS_WARN);
    }
    PopBtn();

    ImGui::SameLine(0.0f, group_gap);

    /* ---------------- Jump ---------------- */
    ImGui::TextDisabled("JUMP");
    ImGui::SameLine();
    ImGui::TextUnformatted("Goto:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("##goto", g_goto_buf, sizeof(g_goto_buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        DoGoto(core);
    }
    if (ImGui::IsItemActive()) g_focus_field = FOCUS_GOTO;
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Go")) DoGoto(core);
    PopBtn();

    ImGui::SameLine(0.0f, group_gap);

    /* ---------------- Search / Edit ---------------- */
    ImGui::TextDisabled("SEARCH/EDIT");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("##search", g_search_buf, sizeof(g_search_buf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        DoSearch(core);
    }
    if (ImGui::IsItemActive()) g_focus_field = FOCUS_SEARCH;
    ImGui::SameLine();
    PushPrimary();
    if (ImGui::Button("Find")) DoSearch(core);
    PopBtn();
    ImGui::SameLine();
    PushSecondary();
    if (ImGui::Button("Undo")) DoUndo(core);
    PopBtn();

    /* Help toggle, right-aligned-ish with extra gap. */
    ImGui::SameLine(0.0f, group_gap);
    PushSecondary();
    if (ImGui::Button(g_show_help ? "Hide ?" : "?")) {
        g_show_help = !g_show_help;
    }
    PopBtn();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle quick reference (F1)");
}

/* ------------------------------------------------------------------ */
/* Badge / hint helpers                                               */
/* ------------------------------------------------------------------ */
/* Renders a small colored pill that lines up with surrounding         *
 * frame-padded text (call AlignTextToFramePadding once per row).      */
void Badge(const char* text, ImVec4 bg, ImVec4 fg, float alpha = 1.0f) {
    bg.w *= alpha;
    fg.w *= alpha;
    ImVec2 ts      = ImGui::CalcTextSize(text);
    ImVec2 padding = ImVec2(7.0f, 2.0f);
    ImVec2 p0      = ImGui::GetCursorScreenPos();
    /* Lift the badge up so its text baseline matches plain Text() that *
     * was aligned with AlignTextToFramePadding earlier in the line.    */
    float frame_pad_y = ImGui::GetStyle().FramePadding.y;
    p0.y += frame_pad_y - padding.y;

    ImVec2 size(ts.x + padding.x * 2, ts.y + padding.y * 2);
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), 4.0f);
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(fg.x, fg.y, fg.z, 0.35f)),
                4.0f, 0, 1.0f);
    dl->AddText(ImVec2(p0.x + padding.x, p0.y + padding.y),
                ImGui::GetColorU32(fg), text);

    ImGui::Dummy(size);
}

const char* GetContextualHint(const HexEditorCore& core) {
    if (g_selected_byte >= 0)
        return "Type 1-2 hex digits, Enter to commit, Esc to cancel";
    if (g_focus_field == FOCUS_GOTO)
        return "Hex offset (e.g. 1A0), Enter to jump";
    if (g_focus_field == FOCUS_SEARCH)
        return "Hex bytes (e.g. DE AD BE EF), Enter to find";
    if (core.IsReadOnly())
        return "Read-only file: click bytes to inspect, F1 for shortcuts";
    return "Click any byte to edit, PgUp/PgDn to page, F1 for shortcuts";
}

/* ------------------------------------------------------------------ */
/* Help / onboarding panel                                            */
/* ------------------------------------------------------------------ */
/* Drawn inside the grid child window after the rows when there is    *
 * leftover vertical space. Goes away as soon as the user does        *
 * anything; recall with F1 or the toolbar button.                    */
void RenderHelpPanel(float visibility) {
    float remaining = ImGui::GetContentRegionAvail().y;
    if (remaining < 90.0f) return;

    ImGui::Dummy(ImVec2(0, 12));

    const float pad_x = 16.0f, pad_y = 12.0f;
    const float panel_w = 460.0f;
    float avail_w = ImGui::GetContentRegionAvail().x;
    float indent  = (avail_w > panel_w) ? (avail_w - panel_w) * 0.5f : 0.0f;
    if (indent > 0.0f) ImGui::Indent(indent);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const char* lines[] = {
        "Quick reference",
        "",
        "  Click byte         Edit hex value in place",
        "  Enter              Commit edit",
        "  Esc                Cancel edit",
        "  PgUp / PgDn        Previous / next page",
        "  Ctrl+Z             Undo last edit",
        "  F1                 Toggle this panel",
        "  Goto field         Jump to a hex offset",
        "  Search field       Find a hex byte sequence",
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    float line_h = ImGui::GetTextLineHeightWithSpacing();
    float panel_h = pad_y * 2 + line_h * (float)n;
    float shown_h = panel_h * visibility;

    if (shown_h > remaining - 8.0f || shown_h < 20.0f) {
        if (indent > 0.0f) ImGui::Unindent(indent);
        return;
    }

    ImVec2 p1(p0.x + panel_w, p0.y + shown_h);
    dl->AddRectFilled(ImVec2(p0.x + 4.0f, p0.y + 5.0f), ImVec2(p1.x + 6.0f, p1.y + 8.0f),
                      ImGui::GetColorU32(ImVec4(0.00f, 0.00f, 0.00f, 0.20f * visibility)), 8.0f);
    dl->AddRectFilled(ImVec2(p0.x + 2.0f, p0.y + 3.0f), ImVec2(p1.x + 3.0f, p1.y + 4.0f),
                      ImGui::GetColorU32(ImVec4(0.00f, 0.00f, 0.00f, 0.12f * visibility)), 8.0f);
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.13f, 0.15f, 0.19f, 0.96f * visibility)), 6.0f);
    dl->AddRect      (p0, p1, ImGui::GetColorU32(ImVec4(0.32f, 0.40f, 0.55f, 0.95f * visibility)), 6.0f, 0, 1.5f);

    /* Close (X) button in the top-right corner. */
    const float x_sz = 18.0f;
    ImVec2 x_pos(p1.x - x_sz - 6.0f, p0.y + 6.0f);
    ImGui::SetCursorScreenPos(x_pos);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.20f, 0.20f, 0.85f * visibility));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.12f, 0.12f, 1.00f * visibility));
    if (visibility <= 0.0f) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("##help_close", ImVec2(x_sz, x_sz))) {
        g_show_help = false;
    }
    if (visibility <= 0.0f) {
        ImGui::EndDisabled();
    }
    ImGui::PopStyleColor(3);
    /* Draw the X glyph on top of the (transparent) button. */
    ImU32 x_col = ImGui::GetColorU32(ImVec4(0.85f, 0.88f, 0.95f, visibility));
    float pad   = 5.0f;
    dl->AddLine(ImVec2(x_pos.x + pad,        x_pos.y + pad),
                ImVec2(x_pos.x + x_sz - pad, x_pos.y + x_sz - pad), x_col, 1.5f);
    dl->AddLine(ImVec2(x_pos.x + x_sz - pad, x_pos.y + pad),
                ImVec2(x_pos.x + pad,        x_pos.y + x_sz - pad), x_col, 1.5f);

    /* Now lay out the text rows. SetCursorScreenPos jumped us around   *
     * for the close button, so reset to just below p0 first.           */
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + pad_y));
    for (int i = 0; i < n; ++i) {
        float line_y = p0.y + pad_y + line_h * (float)i;
        if (line_y + line_h > p1.y - pad_y * 0.25f) break;
        ImGui::SetCursorScreenPos(
            ImVec2(p0.x + pad_x, line_y));
        if (i == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.97f, 1.0f, visibility));
            ImGui::TextUnformatted(lines[i]);
            ImGui::PopStyleColor();
        } else if (lines[i][0] == '\0') {
            /* spacer row, nothing to draw */
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.66f, 0.74f, visibility));
            ImGui::TextUnformatted(lines[i]);
            ImGui::PopStyleColor();
        }
    }

    /* Park the cursor below the panel so anything that follows lands  *
     * in the right place.                                              */
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 4.0f));

    if (indent > 0.0f) ImGui::Unindent(indent);
}

/* ------------------------------------------------------------------ */
/* Status bar                                                         */
/* ------------------------------------------------------------------ */
void RenderStatusBar(HexEditorCore& core) {
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();

    ImGui::Text("0x%08" PRIX64 "   Page %" PRId64 "/%" PRId64 "   %" PRId64 " B",
                (uint64_t)core.GetCurrentOffset(),
                core.GetPageNumber(),
                core.GetTotalPages(),
                core.GetFileSize());

    /* Mode / state badges */
    const ImVec4 neutral_bg(0.20f, 0.22f, 0.27f, 1.00f);
    const ImVec4 neutral_fg(0.85f, 0.88f, 0.95f, 1.00f);
    const ImVec4 ok_bg     (0.16f, 0.36f, 0.20f, 1.00f);
    const ImVec4 ok_fg     (0.80f, 1.00f, 0.85f, 1.00f);
    const ImVec4 warn_bg   (0.45f, 0.30f, 0.10f, 1.00f);
    const ImVec4 warn_fg   (1.00f, 0.92f, 0.75f, 1.00f);
    const ImVec4 err_bg    (0.50f, 0.15f, 0.18f, 1.00f);
    const ImVec4 err_fg    (1.00f, 0.85f, 0.85f, 1.00f);
    const ImVec4 read_bg   (0.35f, 0.30f, 0.10f, 1.00f);
    const ImVec4 read_fg   (1.00f, 0.95f, 0.70f, 1.00f);

    ImGui::SameLine(0, 14);
    Badge("OVR", neutral_bg, neutral_fg);

    ImGui::SameLine(0, 6);
    if (core.IsReadOnly()) Badge("READ-ONLY", read_bg, read_fg);
    else                   Badge("EDIT",      neutral_bg, neutral_fg);

    ImGui::SameLine(0, 6);
    int undos = core.GetUndoCount();
    if (undos > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "MODIFIED %d", undos);
        Badge(buf, warn_bg, warn_fg);
    } else {
        Badge("CLEAN", ok_bg, ok_fg);
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
    if (g_status_timer > 0.0f) {
        ImVec4 bg, fg;
        switch (g_status_kind) {
            case STATUS_OK:    bg = ok_bg;      fg = ok_fg;      break;
            case STATUS_WARN:  bg = warn_bg;    fg = warn_fg;    break;
            case STATUS_ERROR: bg = err_bg;     fg = err_fg;     break;
            default:           bg = neutral_bg; fg = neutral_fg; break;
        }
        const float fade_window = 0.5f;
        float alpha_raw = g_status_timer / fade_window;
        if (alpha_raw < 0.0f) alpha_raw = 0.0f;
        if (alpha_raw > 1.0f) alpha_raw = 1.0f;
        float alpha = (g_status_timer >= fade_window) ? 1.0f : alpha_raw;
        ImGui::SameLine(0, 14);
        Badge(g_status_msg.c_str(), bg, fg, alpha);
        g_status_timer -= ImGui::GetIO().DeltaTime;
    }

    /* Contextual hint, dim, end-of-line. */
    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("%s", GetContextualHint(core));
}

/* ------------------------------------------------------------------ */
/* Keyboard shortcuts                                                 */
/* ------------------------------------------------------------------ */
void HandleShortcuts(HexEditorCore& core) {
    ImGuiIO& io = ImGui::GetIO();

    /* F1 toggles the help panel even when a text field is focused.    */
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) g_show_help = !g_show_help;

    /* Dismiss the help panel on any click that wasn't consumed by a   *
     * widget (so clicking the toolbar `?` button or a hex byte still  *
     * works as expected). Gated on g_show_help so this is a no-op     *
     * once the panel is already hidden.                               */
    if (g_show_help &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() &&
        !ImGui::IsAnyItemActive()) {
        g_show_help = false;
    }

    if (io.WantTextInput) return;

    bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) DoUndo(core);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))  { if (core.PageNext()) MarkInteracted(); }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))    { if (core.PagePrev()) MarkInteracted(); }
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Public entry                                                       */
/* ------------------------------------------------------------------ */
void SetEditorFonts(ImFont* ui_font, ImFont* mono_font) {
    g_ui_font = ui_font;
    g_mono_font = mono_font;
}

void RenderHexEditorUI(HexEditorCore& core) {
    float dt = ImGui::GetIO().DeltaTime;
    float t = dt * 10.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float help_target = g_show_help ? 1.0f : 0.0f;
    g_help_anim = g_help_anim + (help_target - g_help_anim) * t;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    std::string title = "hxediter — " + core.GetFilename();
    ImGui::Begin(title.c_str(), nullptr, flags);

    /* ----------- Accessibility / contrast tweaks ------------------- *
     * Brighter foregrounds, visible borders and a 1px frame border so *
     * keyboard focus on inputs/buttons is unambiguous. Pushed inside  *
     * the window so they only affect this editor and not other ImGui *
     * windows the host might draw.                                    */
    ImGui::PushStyleColor(ImGuiCol_Text,            ImVec4(0.94f, 0.95f, 0.97f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,    ImVec4(0.62f, 0.66f, 0.74f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,         ImVec4(0.13f, 0.15f, 0.19f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  ImVec4(0.20f, 0.24f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,   ImVec4(0.26f, 0.34f, 0.48f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,          ImVec4(0.36f, 0.42f, 0.54f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_NavCursor,       ImVec4(0.40f, 0.70f, 1.00f, 1.00f));
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding,   3.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_ItemSpacing,     ImVec2(8.0f, 4.0f));

    /* Reset focus tracker each frame; it is re-set inside the toolbar *
     * and grid by IsItemActive checks so the contextual hint can      *
     * follow keyboard focus.                                          */
    g_focus_field = (g_selected_byte >= 0) ? FOCUS_BYTE : FOCUS_NONE;

    if (g_ui_font) ImGui::PushFont(g_ui_font);
    RenderToolbar(core);
    if (g_ui_font) ImGui::PopFont();
    ImGui::Separator();

    /* Layout is shared between the header strip (rendered in the     *
     * parent window so it stays put while the body scrolls) and the  *
     * grid body (rendered inside the child). Zero left padding on    *
     * the child window keeps both rows aligned to the same X.        */
    if (g_mono_font) ImGui::PushFont(g_mono_font);
    HexLayout layout = ComputeHexLayout(ImGui::GetContentRegionAvail().x);
    RenderHexHeader(layout);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
    ImGui::BeginChild("##hexview",
                      ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      false,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    RenderHexGrid(core, layout);
    ImGui::EndChild();
    if (g_mono_font) ImGui::PopFont();

    if (g_ui_font) ImGui::PushFont(g_ui_font);
    RenderStatusBar(core);
    if (g_ui_font) ImGui::PopFont();

    HandleShortcuts(core);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
    ImGui::End();
}
