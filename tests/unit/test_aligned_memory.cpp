// Regression tests for the aligned allocation pair (master prompt §9.3).
// Allocation and free must come from the same facility; a mismatch corrupts
// the MSVC CRT heap. Running many iterations under the debug CRT/ASan is the
// point of this test.

#include "TestHarness.h"

#include <portable/AlignedMemory.h>

#include <cstdint>
#include <cstring>

using namespace openmoonray_anari::portable;

int main()
{
    const std::size_t alignments[] = {16, 32, 64, 128};

    for (std::size_t alignment : alignments) {
        for (int iteration = 0; iteration < 250; ++iteration) {
            const std::size_t size = 1 + static_cast<std::size_t>(iteration) * 7;
            void *p = alignedMalloc(size, alignment);
            CHECK(p != nullptr);
            if (p != nullptr) {
                CHECK_EQ(reinterpret_cast<std::uintptr_t>(p) % alignment, 0u);
                std::memset(p, 0xAB, size);
                alignedFree(p);
            }
        }
    }

    // Freeing nullptr must be a no-op on both implementations.
    alignedFree(nullptr);

    return TEST_MAIN_RESULT();
}
