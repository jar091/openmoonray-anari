// Regression test for portable sincos.
// The historical Windows port's sincos called itself
// (HISTORICAL_WINDOWS_PORT_AUDIT.md §1 item #3 — infinite recursion). This
// test terminating at all proves non-recursion; values are checked against
// std::sin/std::cos.

#include "TestHarness.h"

#include <portable/MathCompat.h>

#include <cmath>

using namespace openmoonray_anari::portable;

int main()
{
    for (int i = -1000; i <= 1000; ++i) {
        const double angle = static_cast<double>(i) * 0.01;
        double s = 0.0;
        double c = 0.0;
        sincos(angle, &s, &c);
        CHECK(std::abs(s - std::sin(angle)) < 1e-15);
        CHECK(std::abs(c - std::cos(angle)) < 1e-15);

        const float angleF = static_cast<float>(angle);
        float sf = 0.0f;
        float cf = 0.0f;
        sincosf(angleF, &sf, &cf);
        CHECK(std::abs(sf - std::sin(angleF)) < 1e-6f);
        CHECK(std::abs(cf - std::cos(angleF)) < 1e-6f);
    }

    return TEST_MAIN_RESULT();
}
