// Milestone 5 integration tests: anari.load_library / anari.create_device /
// anari.release_device / anari.invalid_library.
//
// External-facing contract only: the device is loaded dynamically through the
// ANARI loader (anariLoadLibrary("moonray")) — no internal adapter classes
// are linked (master prompt §31.2).

#include <anari/anari.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
            ++failures;                                                                 \
        }                                                                               \
    } while (0)

void statusFunc(const void * /*userData*/, ANARIDevice /*device*/, ANARIObject /*source*/,
                ANARIDataType /*sourceType*/, ANARIStatusSeverity severity,
                ANARIStatusCode /*code*/, const char *message)
{
    if (severity <= ANARI_SEVERITY_WARNING) {
        std::printf("[anari status %d] %s\n", static_cast<int>(severity), message);
    }
}

} // namespace

int main()
{
    // Negative case first: a nonexistent library must fail cleanly.
    {
        ANARILibrary bad = anariLoadLibrary("definitely_missing_xyz", statusFunc, nullptr);
        CHECK(bad == nullptr);
    }

    ANARILibrary lib = anariLoadLibrary("moonray", statusFunc, nullptr);
    CHECK(lib != nullptr);
    if (lib == nullptr) {
        std::printf("cannot continue without the moonray library\n");
        return 1;
    }

    ANARIDevice device = anariNewDevice(lib, "default");
    CHECK(device != nullptr);

    if (device != nullptr) {
        // Object creation through the factory dispatch must return handles.
        ANARIWorld world = anariNewWorld(device);
        CHECK(world != nullptr);
        if (world != nullptr) {
            anariRelease(device, world);
        }

        // Retain/release pairing must not destroy prematurely.
        anariRetain(device, device);
        anariRelease(device, device);
        anariRelease(device, device);
    }

    anariUnloadLibrary(lib);

    if (failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
}
