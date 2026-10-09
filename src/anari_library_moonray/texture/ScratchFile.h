// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>

namespace moonray_anari {
namespace scratch {

// Several MoonRay inputs are *files*: `ImageMap` and `EnvLight` read textures
// from disk, `VdbGeometry` reads a volume from a .vdb. ANARI hands the device
// memory, so the device writes it out - see docs/ANARI_MOONRAY_MAPPING.md.
//
// Every file goes into one directory per process (under the system temp
// directory), so parallel runs cannot collide and the whole lot can be found
// in one place, and every write gets a fresh name, which is what keeps
// MoonRay's caches - keyed on the path - from serving the previous contents
// after an update.

// A new, unused path with the given extension (no dot). Returns an empty
// string and sets `error` when the directory could not be created.
std::string newFilePath(const char *tag,
    const void *owner,
    const char *extension,
    std::string &error);

// Best-effort removal; a leftover scratch file is harmless.
void removeFile(const std::string &path);

} // namespace scratch
} // namespace moonray_anari
