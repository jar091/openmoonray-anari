// Portable aligned allocation with a guaranteed matched pair.
//
// Clean reimplementation of the historical posix_memalign/_aligned_malloc
// changes (HISTORICAL_WINDOWS_PORT_AUDIT.md §1, items #6a/#9). The two
// functions live in one header under one condition so the
// _aligned_malloc -> free / malloc -> _aligned_free mismatch class of bug
// (master prompt §9.3) cannot occur by construction.

#pragma once

#include <cstddef>
#include <cstdlib>

namespace openmoonray_anari {
namespace portable {

// alignment must be a power of two and a multiple of sizeof(void*).
// Returns nullptr on failure.
inline void *alignedMalloc(std::size_t size, std::size_t alignment)
{
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    void *p = nullptr;
    if (posix_memalign(&p, alignment, size) != 0) {
        return nullptr;
    }
    return p;
#endif
}

inline void alignedFree(void *p)
{
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

} // namespace portable
} // namespace openmoonray_anari
