// Tests for the DynamicLibrary abstraction (master prompt §10.2):
// valid plugin, missing plugin, missing symbol, error-message quality.
// argv[1] is the path to the test plugin built by CMake
// (omra_test_plugin, exporting omra_test_plugin_value()).

#include "TestHarness.h"

#include <portable/DynamicLibrary.h>

#include <cstring>
#include <string>

using namespace openmoonray_anari::portable;

using PluginValueFn = int (*)();

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: test_dynamic_library <path-to-test-plugin>\n");
        return 2;
    }
    const std::string pluginPath = argv[1];

    // Missing plugin: must fail with a non-empty, facility-correct message.
    {
        DynamicLibrary lib;
        CHECK(!lib.open("no_such_directory/no_such_plugin_xyz"));
        CHECK(!lib.isOpen());
        CHECK(!lib.errorMessage().empty());
    }

    // Valid plugin: open, resolve, call, close.
    {
        DynamicLibrary lib;
        CHECK(lib.open(pluginPath));
        CHECK(lib.isOpen());

        void *raw = lib.symbol("omra_test_plugin_value");
        CHECK(raw != nullptr);
        if (raw != nullptr) {
            PluginValueFn fn = reinterpret_cast<PluginValueFn>(raw);
            CHECK_EQ(fn(), 42);
        }

        // Missing symbol: nullptr + non-empty error, library stays open.
        CHECK(lib.symbol("omra_no_such_symbol") == nullptr);
        CHECK(!lib.errorMessage().empty());
        CHECK(lib.isOpen());

        lib.close();
        CHECK(!lib.isOpen());
    }

    // Move semantics keep exactly one owner.
    {
        DynamicLibrary a;
        CHECK(a.open(pluginPath));
        DynamicLibrary b(std::move(a));
        CHECK(b.isOpen());
        CHECK(!a.isOpen());
        CHECK(b.symbol("omra_test_plugin_value") != nullptr);
    }

    // Platform suffix sanity.
#if defined(_WIN32)
    CHECK_EQ(std::strcmp(DynamicLibrary::platformSuffix(), ".dll"), 0);
#elif defined(__APPLE__)
    CHECK_EQ(std::strcmp(DynamicLibrary::platformSuffix(), ".dylib"), 0);
#else
    CHECK_EQ(std::strcmp(DynamicLibrary::platformSuffix(), ".so"), 0);
#endif

    return TEST_MAIN_RESULT();
}
