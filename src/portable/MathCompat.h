// Portable math compatibility helpers.
//
// sincos: glibc provides ::sincos as an extension; MSVC and macOS do not.
// The historical port's replacement recursed into itself
// (HISTORICAL_WINDOWS_PORT_AUDIT.md §1, item #3 — confirmed infinite
// recursion). This is the correct, boring implementation; regression-tested
// for termination and value correctness in tests/unit.

#pragma once

#include <cmath>

namespace openmoonray_anari {
namespace portable {

inline void sincos(double angle, double *outSin, double *outCos)
{
    *outSin = std::sin(angle);
    *outCos = std::cos(angle);
}

inline void sincosf(float angle, float *outSin, float *outCos)
{
    *outSin = std::sin(angle);
    *outCos = std::cos(angle);
}

} // namespace portable
} // namespace openmoonray_anari
