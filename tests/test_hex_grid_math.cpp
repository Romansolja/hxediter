#include "test_runner.h"
#include "ui/hex_grid_math.h"

#include <climits>
#include <cstdint>

using ui::ComputeTotalRows;
using ui::MinBytesPerLineForAddressability;

static void test_total_rows_basic() {
    HX_TEST_ASSERT_EQ(ComputeTotalRows(0,   16), 0);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(1,   16), 1);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(15,  16), 1);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(16,  16), 1);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(17,  16), 2);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(32,  16), 2);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(33,  16), 3);
}

static void test_total_rows_degenerate() {
    HX_TEST_ASSERT_EQ(ComputeTotalRows(-1,  16), 0);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(100,  0), 0);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(100, -4), 0);
}

static void test_total_rows_no_overflow_near_int64_max() {
    // The pre-fix expression (file_size + bpl - 1) / bpl would wrap to a
    // negative quotient for these inputs; result must clamp cleanly to INT_MAX.
    HX_TEST_ASSERT_EQ(ComputeTotalRows(INT64_MAX, 16), INT_MAX);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(INT64_MAX,  1), INT_MAX);
    HX_TEST_ASSERT_EQ(ComputeTotalRows(INT64_MAX, 256), INT_MAX);
    // Smaller but still over INT_MAX rows.
    int64_t too_many = (int64_t)INT_MAX * 16 + 1;
    HX_TEST_ASSERT_EQ(ComputeTotalRows(too_many, 16), INT_MAX);
    // Just at the boundary.
    HX_TEST_ASSERT_EQ(ComputeTotalRows((int64_t)INT_MAX * 16, 16), INT_MAX);
}

static void test_min_bpl_for_addressability() {
    HX_TEST_ASSERT_EQ(MinBytesPerLineForAddressability(0),  1);
    HX_TEST_ASSERT_EQ(MinBytesPerLineForAddressability(1),  1);
    HX_TEST_ASSERT_EQ(MinBytesPerLineForAddressability(-1), 1);
    HX_TEST_ASSERT_EQ(MinBytesPerLineForAddressability(INT_MAX), 1);
    // One byte past INT_MAX at BPL=1 needs BPL>=2.
    HX_TEST_ASSERT_EQ(MinBytesPerLineForAddressability((int64_t)INT_MAX + 1), 2);
    HX_TEST_ASSERT_EQ(MinBytesPerLineForAddressability((int64_t)INT_MAX * 2), 2);
    // file_size = INT64_MAX must yield BPL ≤ INT_MAX (cap, not overflow).
    int bpl = MinBytesPerLineForAddressability(INT64_MAX);
    HX_TEST_ASSERT(bpl >= 1);
    HX_TEST_ASSERT(bpl <= INT_MAX);
}

static void test_min_bpl_keeps_rows_in_range() {
    // Contract: for file_size up to INT_MAX*INT_MAX (~4.6 EB) the suggested BPL
    // brings total_rows under INT_MAX, so the clipper clamp doesn't engage and
    // every byte stays reachable. Spot-check a few sizes that previously lost
    // bytes past INT_MAX rows.
    int64_t sizes[] = {
        (int64_t)INT_MAX,
        (int64_t)INT_MAX * 4 + 7,
        (int64_t)INT_MAX * 16,
        (int64_t)INT_MAX * 100,
        (int64_t)INT_MAX * 256,
    };
    for (int64_t fs : sizes) {
        int bpl = MinBytesPerLineForAddressability(fs);
        HX_TEST_ASSERT(bpl >= 1);
        // Compute ceil(fs / bpl) without overflow.
        int64_t rows = fs / bpl;
        if (fs % bpl != 0) rows += 1;
        HX_TEST_ASSERT(rows <= (int64_t)INT_MAX);
        HX_TEST_ASSERT_EQ(ComputeTotalRows(fs, bpl), (int)rows);
    }
}

int main() {
    test_total_rows_basic();
    test_total_rows_degenerate();
    test_total_rows_no_overflow_near_int64_max();
    test_min_bpl_for_addressability();
    test_min_bpl_keeps_rows_in_range();
    return tests::run_all();
}
