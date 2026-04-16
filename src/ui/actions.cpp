/* actions.cpp — Hex parsing helpers and editor command handlers. */

#include "ui/actions.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

namespace ui {

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

void DoGoto(GuiState& s, HexEditorCore& core) {
    uint64_t off;
    if (!ParseHexU64(s.goto_buf, &off)) {
        s.SetStatus("Invalid offset", GuiState::STATUS_ERROR);
        return;
    }
    if (!core.GoToOffset((int64_t)off)) {
        s.SetStatus("Offset out of range", GuiState::STATUS_ERROR);
        return;
    }
    s.MarkInteracted();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Jumped to 0x%" PRIX64, (uint64_t)off);
    s.SetStatus(buf, GuiState::STATUS_OK);
}

void DoSearch(GuiState& s, HexEditorCore& core) {
    auto pat = ParseHexBytes(s.search_buf);
    if (pat.empty()) {
        s.SetStatus("Invalid hex pattern", GuiState::STATUS_ERROR);
        return;
    }
    auto res = core.Search(pat);
    if (!res) {
        s.last_hit = -1;
        s.SetStatus("Pattern not found", GuiState::STATUS_WARN);
        return;
    }
    s.MarkInteracted();
    s.last_hit = res->offset;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Found at 0x%" PRIX64, (uint64_t)res->offset);
    s.SetStatus(buf, GuiState::STATUS_OK);
}

void DoUndo(GuiState& s, HexEditorCore& core) {
    auto res = core.Undo();
    if (!res) {
        s.SetStatus("Nothing to undo", GuiState::STATUS_WARN);
        return;
    }
    s.MarkInteracted();
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "Undid 0x%" PRIX64 ": 0x%02X -> 0x%02X (%d left)",
                  (uint64_t)res->offset, res->undone_val, res->restored_val,
                  res->remaining_undos);
    s.SetStatus(buf, GuiState::STATUS_OK);
}

void CommitEdit(GuiState& s, HexEditorCore& core) {
    if (s.selected_byte < 0) return;
    if (s.edit_buf[0] == '\0') { s.selected_byte = -1; return; }

    unsigned int v = 0;
    if (std::sscanf(s.edit_buf, "%x", &v) != 1 || v > 0xFF) {
        s.SetStatus("Invalid byte value", GuiState::STATUS_ERROR);
        s.selected_byte = -1;
        return;
    }
    auto res = core.EditByte(s.selected_byte, (unsigned char)v);
    if (!res) {
        s.SetStatus("Edit failed (read-only?)", GuiState::STATUS_ERROR);
    } else {
        s.MarkInteracted();
        char buf[80];
        std::snprintf(buf, sizeof(buf),
                      "Edited 0x%" PRIX64 ": 0x%02X -> 0x%02X",
                      (uint64_t)res->offset, res->old_val, res->new_val);
        s.SetStatus(buf, GuiState::STATUS_OK);
    }
    s.selected_byte = -1;
}

} /* namespace ui */
