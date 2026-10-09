// Milestone 6 groundwork: retained-object-model unit tests exercised through
// the PUBLIC ANARI API only (anariLoadLibrary("moonray") — no internal
// adapter classes are linked). One binary, one subcommand per CTest target:
//
//   parameter_store    — set/unset/commit across types; validity property
//   array_lifetime     — shared-with-deleter, managed, and parameter-held
//                        array data ownership (deleter timing is observable)
//   object_references  — retain/release pairing keeps handles usable; the
//                        device reports leaked handles on destruction
//
// commit-snapshot and dirty-propagation tests are deferred to Milestone 7:
// without a rendering path their effects are not observable through the
// external contract, and faking observability is not acceptable (TESTING.md).

#include <anari/anari.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK_MSG(cond, msg)                                                            \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, msg);       \
            ++failures;                                                                 \
        }                                                                               \
    } while (0)

#define CHECK(cond) CHECK_MSG(cond, "")

struct StatusLog {
    std::vector<std::pair<ANARIStatusSeverity, std::string>> entries;

    bool anyErrors() const
    {
        for (const auto &e : entries) {
            if (e.first <= ANARI_SEVERITY_ERROR) {
                return true;
            }
        }
        return false;
    }

    bool anyContains(const char *needle) const
    {
        for (const auto &e : entries) {
            if (e.second.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

void statusFunc(const void *userData, ANARIDevice, ANARIObject, ANARIDataType,
                ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
    auto *log = static_cast<StatusLog *>(const_cast<void *>(userData));
    log->entries.emplace_back(severity, message ? message : "");
    if (severity <= ANARI_SEVERITY_WARNING) {
        std::printf("[anari %d] %s\n", static_cast<int>(severity), message);
    }
}

bool getBoolProperty(ANARIDevice d, ANARIObject o, const char *name)
{
    // ANARI_BOOL properties are written as a single byte by helium; read
    // into a zero-initialized int so any nonzero write is observable.
    int value = 0;
    anariGetProperty(d, o, name, ANARI_BOOL, &value, sizeof(value), ANARI_WAIT);
    return value != 0;
}

// --- subcommands ------------------------------------------------------------

void testParameterStore(ANARIDevice d)
{
    ANARIRenderer renderer = anariNewRenderer(d, "default");
    CHECK(renderer != nullptr);

    const float background[4] = {0.1f, 0.2f, 0.3f, 1.0f};
    anariSetParameter(d, renderer, "background", ANARI_FLOAT32_VEC4, background);
    anariSetParameter(d, renderer, "name", ANARI_STRING, "test-renderer");
    const int one = 1;
    anariSetParameter(d, renderer, "pixelSamples", ANARI_INT32, &one);
    anariCommitParameters(d, renderer);

    // Re-set after commit and recommit — parameter staging must accept both.
    const float background2[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    anariSetParameter(d, renderer, "background", ANARI_FLOAT32_VEC4, background2);
    anariCommitParameters(d, renderer);

    anariUnsetParameter(d, renderer, "background");
    anariUnsetAllParameters(d, renderer);
    anariCommitParameters(d, renderer);
    anariRelease(d, renderer);

    // Validity is observable: known subtypes are valid, unknown are not.
    ANARIWorld world = anariNewWorld(d);
    CHECK_MSG(getBoolProperty(d, world, "valid"), "world must report valid==true");
    anariRelease(d, world);

    ANARICamera bogus = anariNewCamera(d, "definitely_not_a_camera_subtype");
    CHECK_MSG(bogus != nullptr, "unknown subtypes still yield handles");
    CHECK_MSG(!getBoolProperty(d, bogus, "valid"),
              "unknown camera subtype must report valid==false");
    anariRelease(d, bogus);
}

void testArrayLifetime(ANARIDevice d)
{
    static int deleterCalls = 0;
    static float appMemory[16];
    deleterCalls = 0;

    auto deleter = [](const void * /*userData*/, const void *mem) {
        CHECK_MSG(mem == appMemory, "deleter must receive the app pointer");
        ++deleterCalls;
    };

    // Captured (shared + deleter) ownership: app memory is mapped in place,
    // the deleter fires exactly once when the array dies.
    {
        ANARIArray1D arr =
            anariNewArray1D(d, appMemory, deleter, nullptr, ANARI_FLOAT32, 16);
        CHECK(arr != nullptr);

        void *mapped = anariMapArray(d, arr);
        CHECK_MSG(mapped == appMemory,
                  "captured array must map to the application memory");
        anariUnmapArray(d, arr);

        CHECK_MSG(deleterCalls == 0, "deleter must not fire while array is alive");
        anariRelease(d, arr);
        CHECK_MSG(deleterCalls == 1, "deleter must fire exactly once on release");
    }

    // Parameter-held reference: releasing the handle must NOT free app
    // memory while another object's parameter still references the array.
    deleterCalls = 0;
    {
        ANARIArray1D arr =
            anariNewArray1D(d, appMemory, deleter, nullptr, ANARI_FLOAT32, 16);
        ANARIWorld world = anariNewWorld(d);
        anariSetParameter(d, world, "test.data", ANARI_ARRAY1D, &arr);
        anariRelease(d, arr);
        CHECK_MSG(deleterCalls == 0,
                  "parameter reference must keep app memory alive");
        anariUnsetParameter(d, world, "test.data");
        anariRelease(d, world);
        CHECK_MSG(deleterCalls == 1,
                  "deleter must fire once the last reference is gone");
    }

    // Managed ownership (no app memory): map/write/unmap round-trips.
    {
        ANARIArray1D arr =
            anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT32, 8);
        CHECK(arr != nullptr);

        auto *data = static_cast<uint32_t *>(anariMapArray(d, arr));
        CHECK_MSG(data != nullptr, "managed array must be mappable");
        for (uint32_t i = 0; i < 8; ++i) {
            data[i] = i * 7u;
        }
        anariUnmapArray(d, arr);

        auto *again = static_cast<uint32_t *>(anariMapArray(d, arr));
        CHECK_MSG(again != nullptr, "second map must succeed");
        bool intact = true;
        for (uint32_t i = 0; i < 8; ++i) {
            intact = intact && (again[i] == i * 7u);
        }
        CHECK_MSG(intact, "managed array contents must survive unmap/map");
        anariUnmapArray(d, arr);
        anariRelease(d, arr);
    }
}

void testObjectReferences(ANARIDevice d, StatusLog &log)
{
    // Retain/release pairing: the handle stays usable until the last release.
    ANARIWorld world = anariNewWorld(d);
    anariRetain(d, world);
    anariRetain(d, world);
    anariRelease(d, world);
    anariRelease(d, world);
    CHECK_MSG(getBoolProperty(d, world, "valid"),
              "handle must remain usable while a reference is held");
    anariRelease(d, world);

    // Leak detection: an unreleased handle must be reported when the device
    // is destroyed (helium reports "detected N leaked ..." warnings).
    ANARICamera leaked = anariNewCamera(d, "perspective");
    (void)leaked;
    const size_t before = log.entries.size();
    anariRelease(d, d); // final device release — destruction runs here
    bool leakReported = false;
    for (size_t i = before; i < log.entries.size(); ++i) {
        if (log.entries[i].second.find("leaked") != std::string::npos) {
            leakReported = true;
        }
    }
    CHECK_MSG(leakReported, "device destruction must report the leaked camera");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: %s <parameter_store|array_lifetime|object_references>\n",
                    argv[0]);
        return 2;
    }
    const std::string mode = argv[1];

    StatusLog log;
    ANARILibrary lib = anariLoadLibrary("moonray", statusFunc, &log);
    CHECK(lib != nullptr);
    if (!lib) {
        return 1;
    }
    ANARIDevice device = anariNewDevice(lib, "default");
    CHECK(device != nullptr);
    if (!device) {
        return 1;
    }

    bool deviceReleased = false;
    if (mode == "parameter_store") {
        testParameterStore(device);
    } else if (mode == "array_lifetime") {
        testArrayLifetime(device);
    } else if (mode == "object_references") {
        testObjectReferences(device, log);
        deviceReleased = true; // released inside (leak check)
    } else {
        std::printf("unknown mode: %s\n", mode.c_str());
        return 2;
    }

    if (!deviceReleased) {
        anariRelease(device, device);
        CHECK_MSG(!log.anyContains("leaked"),
                  "clean teardown must not report leaks");
    }
    anariUnloadLibrary(lib);

    if (mode != "object_references") {
        CHECK_MSG(!log.anyErrors(), "no ERROR/FATAL statuses expected");
    }

    if (failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
}
