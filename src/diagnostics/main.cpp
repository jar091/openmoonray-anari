// openmoonray_anari_diag — developer diagnostics tool (master prompt §48).
//
// Milestone 0 scope: report project version, pinned dependency baseline,
// platform, and compiler. Later milestones extend this with the resolved
// runtime information (DSO strategy, ANARI extensions, frame channels,
// XPU availability) once those subsystems exist.

#include <openmoonray_anari/Version.h>

#include <cstdio>

namespace {

const char *platformName()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

void printCompiler()
{
#if defined(_MSC_VER)
    std::printf("compiler: MSVC %d\n", _MSC_VER);
#elif defined(__clang__)
    std::printf("compiler: Clang %d.%d.%d\n", __clang_major__, __clang_minor__,
                __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    std::printf("compiler: unknown\n");
#endif
}

} // namespace

int main()
{
    std::printf("openmoonray-anari version: %s\n", OPENMOONRAY_ANARI_VERSION_STRING);
    std::printf("platform: %s\n", platformName());
    printCompiler();
    std::printf("pointer width: %zu bits\n", sizeof(void *) * 8);
    std::printf("sizeof(long): %zu\n", sizeof(long));

    std::printf("OpenMoonRay baseline: %s\n", OPENMOONRAY_ANARI_BASELINE_OPENMOONRAY);
    std::printf("scene_rdl2 baseline: %s\n", OPENMOONRAY_ANARI_BASELINE_SCENE_RDL2);
    std::printf("moonray baseline: %s\n", OPENMOONRAY_ANARI_BASELINE_MOONRAY);
    std::printf("moonshine baseline: %s\n", OPENMOONRAY_ANARI_BASELINE_MOONSHINE);
    std::printf("ANARI-SDK baseline: %s\n", OPENMOONRAY_ANARI_BASELINE_ANARI_SDK);
    std::printf("ANARI spec: %s\n", OPENMOONRAY_ANARI_BASELINE_ANARI_SPEC);
    std::printf("PyNARI baseline: %s\n", OPENMOONRAY_ANARI_BASELINE_PYNARI);

    return 0;
}
