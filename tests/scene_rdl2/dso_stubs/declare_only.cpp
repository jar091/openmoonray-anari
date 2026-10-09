// Exports rdl2_declare but neither rdl2_create nor rdl2_destroy: a half-ported
// DSO. Proxy mode only needs declare, so this must load there and fail in
// normal mode - the asymmetry is the point.
#if defined(_WIN32)
#define DSO_EXPORT extern "C" __declspec(dllexport)
#else
#define DSO_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// The real signature takes a SceneClass&; the test never reaches a call, only
// the symbol lookup, so an opaque pointer keeps this stub free of scene_rdl2
// headers.
DSO_EXPORT void rdl2_declare(void * /*sceneClass*/) {}
