// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Diagnostics.h"
#include "MoonrayDevice.h"
#include "anari/backend/LibraryImpl.h"
#include "anari_library_moonray_export.h"
#include "anari_library_moonray_queries.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace moonray_anari {

struct MoonrayLibrary : public anari::LibraryImpl
{
  MoonrayLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

// Directory of this library.
static std::filesystem::path libraryDirectory()
{
#ifdef _WIN32
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
              | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&libraryDirectory),
          &module))
    return {};
  wchar_t path[MAX_PATH * 4];
  const DWORD length =
      GetModuleFileNameW(module, path, DWORD(sizeof(path) / sizeof(path[0])));
  if (length == 0 || length >= DWORD(sizeof(path) / sizeof(path[0])))
    return {};
  return std::filesystem::path(std::wstring(path, length)).parent_path();
#else
  Dl_info info;
  if (!dladdr(reinterpret_cast<void *>(&libraryDirectory), &info)
      || !info.dli_fname)
    return {};
  return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

// MoonRay finds its shader DSOs through RDL2_DSO_PATH or next to the
// executable (<exe>/../rdl2dso). An application embedding the device (e.g. a
// DCC loading it as a plugin) has neither, so default RDL2_DSO_PATH to the
// rdl2dso directory of the package this library belongs to.
static void setDefaultDsoPath()
{
  const std::filesystem::path dir = libraryDirectory();
#ifdef _WIN32
  // The shader DSOs are loaded with LOAD_LIBRARY_SEARCH_DEFAULT_DIRS, which
  // doesn't search PATH: MoonRay libraries next to this one that the process
  // hasn't loaded yet must be found through the user DLL directories.
  if (!dir.empty())
    AddDllDirectory(dir.wstring().c_str());
#endif
  if (const char *path = std::getenv("RDL2_DSO_PATH"); path && path[0])
    return;
  if (dir.empty())
    return;
  for (const auto &candidate : {dir.parent_path() / "rdl2dso", dir / "rdl2dso"}) {
    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec)) {
#ifdef _WIN32
      _wputenv_s(L"RDL2_DSO_PATH", candidate.wstring().c_str());
#else
      setenv("RDL2_DSO_PATH", candidate.string().c_str(), 0);
#endif
      return;
    }
  }
}

// MoonRay's XPU mode loads its GPU programs from
// ${REZ_MOONRAY_ROOT}/shaders. Without the variable it looks in "/shaders",
// fails and silently falls back to the CPU, so default it to the package
// this library belongs to.
static void setDefaultMoonrayRoot()
{
  if (const char *root = std::getenv("REZ_MOONRAY_ROOT"); root && root[0])
    return;
  const std::filesystem::path dir = libraryDirectory();
  if (dir.empty())
    return;
  for (const auto &candidate : {dir.parent_path(), dir}) {
    std::error_code ec;
    if (std::filesystem::is_directory(candidate / "shaders", ec)) {
#ifdef _WIN32
      _wputenv_s(L"REZ_MOONRAY_ROOT", candidate.wstring().c_str());
#else
      setenv("REZ_MOONRAY_ROOT", candidate.string().c_str(), 0);
#endif
      return;
    }
  }
}

MoonrayLibrary::MoonrayLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{
  // Opt-in, and as early as possible: the paths it covers can be taken before
  // a device exists.
  installCrashDiagnostics();
  setDefaultDsoPath();
  setDefaultMoonrayRoot();
}

ANARIDevice MoonrayLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new MoonrayDevice(this_library());
}

const char **MoonrayLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  // The same list the device reports through its `extension` property, and
  // the same one MoonrayDefinitions.json generates: an application that asks
  // the *library* what a device supports - which is what the ANARI CTS and
  // anariInfo do - must not get a different answer from one that asks the
  // device. This returned nullptr until Milestone 13, so every such question
  // was answered with "nothing".
  return query_extensions();
}

} // namespace moonray_anari

// Define library entrypoint //////////////////////////////////////////////////

extern "C" MOONRAY_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(moonray, handle, scb, scbPtr)
{
  return (ANARILibrary) new moonray_anari::MoonrayLibrary(handle, scb, scbPtr);
}
