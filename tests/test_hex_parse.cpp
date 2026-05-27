#include "test_runner.h"
#include "ui/actions.h"

#include <climits>
#include <cstdint>

using ui::ParseHexU64;
using ui::ParseHexBytes;

static void test_parse_hex_u64_basic() {
    uint64_t out = 0;
    HX_TEST_ASSERT(ParseHexU64("0", &out));            HX_TEST_ASSERT_EQ(out, (uint64_t)0);
    HX_TEST_ASSERT(ParseHexU64("1", &out));            HX_TEST_ASSERT_EQ(out, (uint64_t)1);
    HX_TEST_ASSERT(ParseHexU64("ff", &out));           HX_TEST_ASSERT_EQ(out, (uint64_t)0xFF);
    HX_TEST_ASSERT(ParseHexU64("FF", &out));           HX_TEST_ASSERT_EQ(out, (uint64_t)0xFF);
    HX_TEST_ASSERT(ParseHexU64("0xFF", &out));         HX_TEST_ASSERT_EQ(out, (uint64_t)0xFF);
    HX_TEST_ASSERT(ParseHexU64("0XAA", &out));         HX_TEST_ASSERT_EQ(out, (uint64_t)0xAA);
    HX_TEST_ASSERT(ParseHexU64("1a2B3c4D", &out));     HX_TEST_ASSERT_EQ(out, (uint64_t)0x1A2B3C4D);
    // Max 64-bit value — 16 hex digits is the boundary case.
    HX_TEST_ASSERT(ParseHexU64("FFFFFFFFFFFFFFFF", &out));
    HX_TEST_ASSERT_EQ(out, UINT64_MAX);
    HX_TEST_ASSERT(ParseHexU64("ffffffffffffffff", &out));
    HX_TEST_ASSERT_EQ(out, UINT64_MAX);
    // Leading zeros stay representable past 16 chars — they don't consume bits.
    HX_TEST_ASSERT(ParseHexU64("000000000000000000FF", &out));
    HX_TEST_ASSERT_EQ(out, (uint64_t)0xFF);
}

static void test_parse_hex_u64_whitespace() {
    uint64_t out = 0;
    HX_TEST_ASSERT(ParseHexU64("  1A", &out));         HX_TEST_ASSERT_EQ(out, (uint64_t)0x1A);
    HX_TEST_ASSERT(ParseHexU64("\t0x2B", &out));       HX_TEST_ASSERT_EQ(out, (uint64_t)0x2B);
    HX_TEST_ASSERT(ParseHexU64("1 2 3 4", &out));      HX_TEST_ASSERT_EQ(out, (uint64_t)0x1234);
}

static void test_parse_hex_u64_invalid() {
    uint64_t out = 0;
    HX_TEST_ASSERT(!ParseHexU64("", &out));
    HX_TEST_ASSERT(!ParseHexU64(nullptr, &out));
    HX_TEST_ASSERT(!ParseHexU64("0x", &out));          // prefix alone
    HX_TEST_ASSERT(!ParseHexU64("   ", &out));         // all whitespace
    HX_TEST_ASSERT(!ParseHexU64("xyz", &out));         // non-hex
    HX_TEST_ASSERT(!ParseHexU64("12g", &out));         // partial non-hex
    HX_TEST_ASSERT(!ParseHexU64("0x ", &out));         // prefix + whitespace, no digits
    HX_TEST_ASSERT(!ParseHexU64("abc", nullptr));      // null out rejected
}

static void test_parse_hex_u64_overflow() {
    // The regression this guards against: a 17-digit hex string used to
    // silently shift its top nibble off the end and present as a smaller value.
    uint64_t out = 0xDEADBEEF;  // sentinel — must not be overwritten on failure
    HX_TEST_ASSERT(!ParseHexU64("10000000000000000", &out));  // 2^64
    HX_TEST_ASSERT_EQ(out, (uint64_t)0xDEADBEEF);
    HX_TEST_ASSERT(!ParseHexU64("FFFFFFFFFFFFFFFFF", &out));  // 17 F's
    HX_TEST_ASSERT_EQ(out, (uint64_t)0xDEADBEEF);
    HX_TEST_ASSERT(!ParseHexU64("123456789ABCDEF01", &out)); // 17 mixed digits
    HX_TEST_ASSERT_EQ(out, (uint64_t)0xDEADBEEF);
    HX_TEST_ASSERT(!ParseHexU64("0x10000000000000000", &out));
    HX_TEST_ASSERT_EQ(out, (uint64_t)0xDEADBEEF);
    // Boundary just inside the limit.
    HX_TEST_ASSERT(ParseHexU64("FFFFFFFFFFFFFFFF", &out));
    HX_TEST_ASSERT_EQ(out, UINT64_MAX);
}

static void test_parse_hex_bytes() {
    auto a = ParseHexBytes("00 FF 12");
    HX_TEST_ASSERT_EQ(a.size(), (size_t)3);
    if (a.size() == 3) {
        HX_TEST_ASSERT_EQ(a[0], (unsigned char)0x00);
        HX_TEST_ASSERT_EQ(a[1], (unsigned char)0xFF);
        HX_TEST_ASSERT_EQ(a[2], (unsigned char)0x12);
    }
    // Commas are separators.
    auto b = ParseHexBytes("DE,AD,BE,EF");
    HX_TEST_ASSERT_EQ(b.size(), (size_t)4);
    if (b.size() == 4) {
        HX_TEST_ASSERT_EQ(b[0], (unsigned char)0xDE);
        HX_TEST_ASSERT_EQ(b[3], (unsigned char)0xEF);
    }
    // Odd nibble count rejected.
    HX_TEST_ASSERT_EQ(ParseHexBytes("0").size(), (size_t)0);
    HX_TEST_ASSERT_EQ(ParseHexBytes("ABC").size(), (size_t)0);
    // Garbage char rejected (entire input thrown away).
    HX_TEST_ASSERT_EQ(ParseHexBytes("AAXX").size(), (size_t)0);
    // Empty / null.
    HX_TEST_ASSERT_EQ(ParseHexBytes("").size(), (size_t)0);
    HX_TEST_ASSERT_EQ(ParseHexBytes(nullptr).size(), (size_t)0);
}

int main() {
    test_parse_hex_u64_basic();
    test_parse_hex_u64_whitespace();
    test_parse_hex_u64_invalid();
    test_parse_hex_u64_overflow();
    test_parse_hex_bytes();
    return tests::run_all();
}
