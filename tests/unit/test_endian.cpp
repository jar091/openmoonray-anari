// Regression tests for endian helpers (RDL2 length prefixes are big-endian
// on disk; the conversion must be byte-exact and involutive).

#include "TestHarness.h"

#include <portable/Endian.h>

#include <cstdint>
#include <cstring>

using namespace openmoonray_anari::portable;

int main()
{
    const uint64_t value = 0x0123456789ABCDEFull;
    const uint64_t big = hostToBigEndian64(value);

    unsigned char bytes[8];
    std::memcpy(bytes, &big, 8);
    const unsigned char expected[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(bytes[i], expected[i]);
    }

    CHECK_EQ(bigEndianToHost64(hostToBigEndian64(value)), value);
    CHECK_EQ(bigEndianToHost64(hostToBigEndian64(0ull)), 0ull);
    CHECK_EQ(bigEndianToHost64(hostToBigEndian64(~0ull)), ~0ull);

    const uint32_t value32 = 0xCAFEBABEu;
    const uint32_t big32 = hostToBigEndian32(value32);
    unsigned char bytes32[4];
    std::memcpy(bytes32, &big32, 4);
    const unsigned char expected32[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(bytes32[i], expected32[i]);
    }
    CHECK_EQ(bigEndianToHost32(hostToBigEndian32(value32)), value32);

    return TEST_MAIN_RESULT();
}
