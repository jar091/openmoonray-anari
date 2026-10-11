// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <string>

namespace moonray_anari {
namespace texture {

// The file-backed way of handing MoonRay a texture, kept as a fallback for
// the in-memory one (texture/MemoryTexture.h, selected in texture/Texture.h):
// MoonRay names its textures - `ImageMap` hands the name to OpenImageIO's
// texture system, `EnvLight` to OIIO::ImageInput::open() - and a name can
// always be a file.
//
// The file is produced with OpenImageIO's make_texture() (the `maketx` code
// path), because MoonRay runs its texture system with `accept_untiled` and
// `accept_unmipped` off: an ordinary image is opened happily and then fails at
// every sample with "unknown oiio sample failure".
//
// Every write goes to a new file name. That is what keeps MoonRay's texture
// cache, which is keyed on the path, from serving the previous pixels after an
// update.

// Writes float pixels (`channels` per pixel, top scanline first - see
// texture/Pixels.h) as a scratch texture and returns its path, or an empty
// string with `error` describing why not.
//
//  * `tag` names the kind of texture in the file name (diagnostics only);
//  * `owner` distinguishes the files of different textures;
//  * `mipmapped` asks for the full MIP pyramid. Shading textures need it (the
//    texture system refuses an unmipped file); an environment map does not,
//    because `EnvLight` reads the file itself, and a pyramid that reaches 1x1
//    makes it log "1x1 mip level not supported at present" for every map.
std::string writeScratchTexture(const char *tag,
    const void *owner,
    size_t width,
    size_t height,
    int channels,
    const float *pixels,
    bool mipmapped,
    std::string &error);

// Best-effort removal of a file written by writeScratchTexture(). A leftover
// scratch file is harmless, so failures are ignored.
void removeScratchTexture(const std::string &path);

} // namespace texture
} // namespace moonray_anari
