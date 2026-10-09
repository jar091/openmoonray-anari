#include "DynamicLibrary.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace openmoonray_anari {
namespace portable {

namespace {

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string &utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), len);
    return wide;
}

std::string lastErrorMessage()
{
    const DWORD code = GetLastError();
    LPSTR buffer = nullptr;
    const DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = "Win32 error " + std::to_string(code);
    if (len != 0 && buffer != nullptr) {
        message += ": ";
        message.append(buffer, len);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
    }
    return message;
}
#endif

} // namespace

DynamicLibrary::~DynamicLibrary()
{
    close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary &&other) noexcept
    : mHandle(other.mHandle), mError(std::move(other.mError))
{
    other.mHandle = nullptr;
}

DynamicLibrary &DynamicLibrary::operator=(DynamicLibrary &&other) noexcept
{
    if (this != &other) {
        close();
        mHandle = other.mHandle;
        mError = std::move(other.mError);
        other.mHandle = nullptr;
    }
    return *this;
}

bool DynamicLibrary::open(const std::string &utf8Path)
{
    close();
#if defined(_WIN32)
    const std::wstring widePath = utf8ToWide(utf8Path);
    HMODULE handle = LoadLibraryExW(widePath.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (handle == nullptr) {
        // Relative paths (no directory component) are rejected by
        // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR; retry with default semantics.
        handle = LoadLibraryExW(widePath.c_str(), nullptr, 0);
    }
    if (handle == nullptr) {
        mError = "failed to load '" + utf8Path + "': " + lastErrorMessage();
        return false;
    }
    mHandle = handle;
#else
    void *handle = dlopen(utf8Path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char *reason = dlerror();
        mError = "failed to load '" + utf8Path + "': " +
                 (reason != nullptr ? reason : "unknown dlopen error");
        return false;
    }
    mHandle = handle;
#endif
    mError.clear();
    return true;
}

void *DynamicLibrary::symbol(const std::string &name)
{
    if (mHandle == nullptr) {
        mError = "symbol lookup on a closed library";
        return nullptr;
    }
#if defined(_WIN32)
    FARPROC address = GetProcAddress(static_cast<HMODULE>(mHandle), name.c_str());
    if (address == nullptr) {
        mError = "symbol '" + name + "' not found: " + lastErrorMessage();
        return nullptr;
    }
    return reinterpret_cast<void *>(address);
#else
    dlerror();
    void *address = dlsym(mHandle, name.c_str());
    if (address == nullptr) {
        const char *reason = dlerror();
        mError = "symbol '" + name + "' not found" +
                 (reason != nullptr ? std::string(": ") + reason : std::string());
        return nullptr;
    }
    return address;
#endif
}

void DynamicLibrary::close()
{
    if (mHandle == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(mHandle));
#else
    dlclose(mHandle);
#endif
    mHandle = nullptr;
}

const char *DynamicLibrary::platformSuffix()
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

} // namespace portable
} // namespace openmoonray_anari
