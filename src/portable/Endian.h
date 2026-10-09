// Portable big-endian conversion helpers.
//
// Clean replacement for the historical htobe64/be64toh macro shim
// (HISTORICAL_WINDOWS_PORT_AUDIT.md §1, item #26): named functions, no
// glibc-name macros, works on MSVC/GCC/Clang including macOS. RDL2 binary
// files store 64-bit length prefixes big-endian; these helpers must never
// change that on-disk format.

#pragma once

#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#include <cstdlib>
#endif

namespace openmoonray_anari {
namespace portable {

inline bool isLittleEndianHost()
{
    const uint16_t probe = 0x0102;
    unsigned char bytes[2];
    std::memcpy(bytes, &probe, 2);
    return bytes[0] == 0x02;
}

inline uint64_t byteSwap64(uint64_t v)
{
#if defined(_MSC_VER)
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v);
#endif
}

inline uint32_t byteSwap32(uint32_t v)
{
#if defined(_MSC_VER)
    return _byteswap_ulong(v);
#else
    return __builtin_bswap32(v);
#endif
}

inline uint64_t hostToBigEndian64(uint64_t v)
{
    return isLittleEndianHost() ? byteSwap64(v) : v;
}

inline uint64_t bigEndianToHost64(uint64_t v)
{
    return hostToBigEndian64(v);
}

inline uint32_t hostToBigEndian32(uint32_t v)
{
    return isLittleEndianHost() ? byteSwap32(v) : v;
}

inline uint32_t bigEndianToHost32(uint32_t v)
{
    return hostToBigEndian32(v);
}

} // namespace portable
} // namespace openmoonray_anari
