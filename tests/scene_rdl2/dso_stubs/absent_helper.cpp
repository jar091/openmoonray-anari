// The dependency that missing_dep.cpp imports and that is deliberately NOT
// installed alongside it.
#if defined(_WIN32)
#define HELPER_EXPORT extern "C" __declspec(dllexport)
#else
#define HELPER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

HELPER_EXPORT int openmoonray_anari_absent_helper() { return 42; }
