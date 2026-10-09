// A valid image that cannot be loaded because a library it imports is not
// installed next to it. CMake builds the helper it links against into a
// separate directory that is never added to the DSO search path or to PATH,
// which is the "forgot to ship a runtime DLL" failure operators hit most.
#if defined(_WIN32)
#define DSO_EXPORT extern "C" __declspec(dllexport)
#else
#define DSO_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Defined in the helper library; the import is what makes this unloadable.
extern "C" int openmoonray_anari_absent_helper();

DSO_EXPORT void rdl2_declare(void * /*sceneClass*/)
{
    // Referenced so the import survives the linker.
    (void)openmoonray_anari_absent_helper();
}
