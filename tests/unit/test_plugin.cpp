// Tiny test plugin for the DynamicLibrary tests.

#if defined(_WIN32)
#define OMRA_TEST_EXPORT __declspec(dllexport)
#else
#define OMRA_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" OMRA_TEST_EXPORT int omra_test_plugin_value()
{
    return 42;
}
