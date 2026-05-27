#pragma once

#include <climits>
#include <cstdint>

namespace ui {

// Number of rows needed to display `file_size` bytes at `bpl` bytes per row.
// Result is clamped to INT_MAX so ImGuiListClipper (which takes int) is safe.
// Uses ceiling division without addition so file_size near INT64_MAX won't wrap.
inline int ComputeTotalRows(int64_t file_size, int bpl) {
    if (file_size <= 0 || bpl <= 0) return 0;
    int64_t rows = file_size / bpl;
    if (file_size % bpl != 0) rows += 1;
    return (rows > static_cast<int64_t>(INT_MAX)) ? INT_MAX : static_cast<int>(rows);
}

// Minimum bytes-per-line that keeps the total row count addressable by
// ImGuiListClipper's `int`. ComputeHexLayout applies max(width_bpl, this) so
// very large files stay fully reachable, even if the row exceeds avail_w
// (the user gets horizontal scroll rather than silently-truncated bytes).
//
// Returns 1 for empty / negative file_size (no addressability constraint).
// Capped at INT_MAX: files beyond INT_MAX * INT_MAX bytes (~4.6 EB) can't be
// fully addressed via the clipper, but no real filesystem holds anything close.
inline int MinBytesPerLineForAddressability(int64_t file_size) {
    if (file_size <= 0) return 1;
    int64_t min_bpl = file_size / static_cast<int64_t>(INT_MAX);
    if (file_size % static_cast<int64_t>(INT_MAX) != 0) min_bpl += 1;
    if (min_bpl < 1)        return 1;
    if (min_bpl > INT_MAX)  return INT_MAX;
    return static_cast<int>(min_bpl);
}

}
