// Minimal test harness for openmoonray-anari unit tests.
// Deliberately dependency-free (no gtest yet); CTest interprets a nonzero
// exit code as failure and the output lists every failed check.

#pragma once

#include <cstdio>

namespace openmoonray_anari {
namespace test {

inline int &failureCount()
{
    static int count = 0;
    return count;
}

} // namespace test
} // namespace openmoonray_anari

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
            ++openmoonray_anari::test::failureCount();                                   \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        if (!((a) == (b))) {                                                            \
            std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);          \
            ++openmoonray_anari::test::failureCount();                                   \
        }                                                                               \
    } while (0)

#define TEST_MAIN_RESULT()                                                              \
    (openmoonray_anari::test::failureCount() == 0                                       \
         ? (std::printf("PASS\n"), 0)                                                   \
         : (std::printf("%d check(s) FAILED\n",                                         \
                        openmoonray_anari::test::failureCount()),                       \
            1))
