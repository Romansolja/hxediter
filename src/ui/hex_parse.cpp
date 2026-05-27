#include "ui/actions.h"

#include <cstdint>

namespace ui {

bool ParseHexU64(const char* s, uint64_t* out) {
    if (!s || !*s || !out) return false;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return false;
    uint64_t v = 0;
    bool any_digit = false;
    while (*s) {
        char c = *s++;
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c == ' ' || c == '\t') continue;
        else return false;
        // Reject 17+ significant hex digits: another nibble would shift bits off the top.
        if (v > (UINT64_MAX >> 4)) return false;
        v = (v << 4) | (uint64_t)d;
        any_digit = true;
    }
    if (!any_digit) return false;
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

}
