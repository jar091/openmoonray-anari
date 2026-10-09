// Regression tests for portable bit primitives.
// The zero-input cases are the exact bugs the historical Windows port had
// (HISTORICAL_WINDOWS_PORT_AUDIT.md §1 item #13: clz(0) returned 31).

#include "TestHarness.h"

#include <portable/Bits.h>

#include <cstdint>

using namespace openmoonray_anari::portable;

namespace {

unsigned naiveClz32(uint32_t v)
{
    for (unsigned i = 0; i < 32; ++i) {
        if (v & (0x80000000u >> i)) {
            return i;
        }
    }
    return 32;
}

unsigned naiveCtz32(uint32_t v)
{
    for (unsigned i = 0; i < 32; ++i) {
        if (v & (1u << i)) {
            return i;
        }
    }
    return 32;
}

unsigned naivePopCount64(uint64_t v)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 64; ++i) {
        n += static_cast<unsigned>((v >> i) & 1u);
    }
    return n;
}

uint64_t nextRandom(uint64_t &state)
{
    // splitmix64 — deterministic across platforms.
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

} // namespace

int main()
{
    // The historical port's bugs, stated explicitly:
    CHECK_EQ(countLeadingZeros32(0u), 32u);
    CHECK_EQ(countTrailingZeros32(0u), 32u);
    CHECK_EQ(countLeadingZeros64(0ull), 64u);
    CHECK_EQ(countTrailingZeros64(0ull), 64u);

    CHECK_EQ(countLeadingZeros32(1u), 31u);
    CHECK_EQ(countLeadingZeros32(0x80000000u), 0u);
    CHECK_EQ(countTrailingZeros32(1u), 0u);
    CHECK_EQ(countTrailingZeros32(0x80000000u), 31u);
    CHECK_EQ(countLeadingZeros64(1ull), 63u);
    CHECK_EQ(countLeadingZeros64(0x8000000000000000ull), 0u);

    for (unsigned bit = 0; bit < 32; ++bit) {
        const uint32_t v = 1u << bit;
        CHECK_EQ(countLeadingZeros32(v), naiveClz32(v));
        CHECK_EQ(countTrailingZeros32(v), naiveCtz32(v));
    }

    CHECK_EQ(popCount32(0u), 0u);
    CHECK_EQ(popCount32(0xFFFFFFFFu), 32u);
    CHECK_EQ(popCount64(0xFFFFFFFFFFFFFFFFull), 64u);

    uint64_t state = 42;
    for (int i = 0; i < 10000; ++i) {
        const uint64_t r = nextRandom(state);
        const uint32_t r32 = static_cast<uint32_t>(r);
        CHECK_EQ(popCount64(r), naivePopCount64(r));
        CHECK_EQ(popCount32(r32), naivePopCount64(r32));
        CHECK_EQ(countLeadingZeros32(r32), naiveClz32(r32));
        CHECK_EQ(countTrailingZeros32(r32), naiveCtz32(r32));
    }

    return TEST_MAIN_RESULT();
}
