// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ScratchFile.h"

// std
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <system_error>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace moonray_anari {
namespace scratch {

namespace fs = std::filesystem;

namespace {

const fs::path &directory()
{
  static const fs::path dir = [] {
#if defined(_WIN32)
    const auto pid = ::_getpid();
#else
    const auto pid = ::getpid();
#endif
    return fs::temp_directory_path()
        / ("anari_moonray_scratch_" + std::to_string(pid));
  }();
  return dir;
}

std::atomic<uint64_t> g_serial{0};

} // namespace

std::string newFilePath(const char *tag,
    const void *owner,
    const char *extension,
    std::string &error)
{
  error.clear();

  std::error_code ec;
  fs::create_directories(directory(), ec);
  if (ec) {
    error = "could not create the scratch directory '" + directory().string()
        + "': " + ec.message();
    return {};
  }

  char name[160];
  std::snprintf(name,
      sizeof(name),
      "%s_%p_%llu.%s",
      tag,
      owner,
      (unsigned long long)(++g_serial),
      extension);
  return (directory() / name).string();
}

void removeFile(const std::string &path)
{
  if (path.empty())
    return;
  std::error_code ec;
  fs::remove(path, ec);
}

} // namespace scratch
} // namespace moonray_anari
