// Portable bit-manipulation primitives.
//
// Clean reimplementation of the historical scene_rdl2 Windows-port bit
// routines (HISTORICAL_WINDOWS_PORT_AUDIT.md §1, items #13-#16). The
// historical port returned 31 for countLeadingZeros(0); the contract here is
// explicit: clz(0) == 32, ctz(0) == 32.

#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace openmoonray_anari {
namespace portable {

inline unsigned countLeadingZeros32(uint32_t v)
{
#if defined(_MSC_VER)
    unsigned long index = 0;
    return _BitScanReverse(&index, v) ? 31u - index : 32u;
#else
    return v ? static_cast<unsigned>(__builtin_clz(v)) : 32u;
#endif
}

inline unsigned countTrailingZeros32(uint32_t v)
{
#if defined(_MSC_VER)
    unsigned long index = 0;
    return _BitScanForward(&index, v) ? index : 32u;
#else
    return v ? static_cast<unsigned>(__builtin_ctz(v)) : 32u;
#endif
}

inline unsigned countLeadingZeros64(uint64_t v)
{
#if defined(_MSC_VER)
    unsigned long index = 0;
    return _BitScanReverse64(&index, v) ? 63u - index : 64u;
#else
    return v ? static_cast<unsigned>(__builtin_clzll(v)) : 64u;
#endif
}

inline unsigned countTrailingZeros64(uint64_t v)
{
#if defined(_MSC_VER)
    unsigned long index = 0;
    return _BitScanForward64(&index, v) ? index : 64u;
#else
    return v ? static_cast<unsigned>(__builtin_ctzll(v)) : 64u;
#endif
}

inline unsigned popCount32(uint32_t v)
{
#if defined(_MSC_VER)
    // SWAR fallback: __popcnt requires POPCNT hardware support and would need
    // a runtime CPUID check to be safe on all x64 machines.
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0F0F0F0Fu;
    return (v * 0x01010101u) >> 24;
#else
    return static_cast<unsigned>(__builtin_popcount(v));
#endif
}

inline unsigned popCount64(uint64_t v)
{
#if defined(_MSC_VER)
    return popCount32(static_cast<uint32_t>(v)) +
           popCount32(static_cast<uint32_t>(v >> 32));
#else
    return static_cast<unsigned>(__builtin_popcountll(v));
#endif
}

} // namespace portable
} // namespace openmoonray_anari
