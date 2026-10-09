// Cross-platform dynamic library abstraction.
//
// Groundwork for the production DSO strategy (master prompt §10.2, ADR 0003):
//
//   Linux/macOS              Windows
//   dlopen()                 LoadLibraryExW()
//   dlsym()                  GetProcAddress()
//   dlclose()                FreeLibrary()
//   .so / .dylib             .dll
//
// Raw platform APIs are never exposed to callers; errors are reported as
// messages from the correct platform facility (dlerror() vs GetLastError()
// — never errno for Win32 failures, see HISTORICAL_WINDOWS_PORT_AUDIT.md §1
// item #20).

#pragma once

#include <string>

namespace openmoonray_anari {
namespace portable {

class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;
    DynamicLibrary(DynamicLibrary &&other) noexcept;
    DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;

    // Opens a library by path (UTF-8). Returns false and sets errorMessage()
    // on failure. Paths with spaces are supported.
    bool open(const std::string &utf8Path);

    // Returns nullptr and sets errorMessage() if the symbol is absent.
    void *symbol(const std::string &name);

    void close();

    bool isOpen() const { return mHandle != nullptr; }
    const std::string &errorMessage() const { return mError; }

    // Platform shared-library suffix: ".dll", ".dylib", or ".so".
    static const char *platformSuffix();

private:
    void *mHandle = nullptr;
    std::string mError;
};

} // namespace portable
} // namespace openmoonray_anari
