// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari.h>
// std
#include <cstdint>
#include <string>

namespace moonray_anari {
namespace texture {

// MoonRay reads textures from *files*: both the shading side (`ImageMap` ->
// OpenImageIO's texture system) and the lighting side (`EnvLight` ->
// `pbr::ImageDistribution`, which opens the file itself) take a path, and
// neither has an in-memory entry point. ANARI hands the device pixels in an
// array, so the device writes them to a scratch file. That is the honest cost
// of the mapping, not a shortcut - see docs/ANARI_MOONRAY_MAPPING.md.
//
// The file is produced with OpenImageIO's make_texture() (the `maketx` code
// path), because MoonRay runs its texture system with `accept_untiled` and
// `accept_unmipped` off: an ordinary image is opened happily and then fails at
// every sample with "unknown oiio sample failure".
//
// Every write goes to a new file name. That is what keeps MoonRay's texture
// cache, which is keyed on the path, from serving the previous pixels after an
// update.

// Writes one ANARI 2D image as a scratch texture and returns its path, or an
// empty string with `error` describing why not.
//
//  * `tag` names the kind of texture in the file name (diagnostics only);
//  * `owner` is the ANARI object the texture belongs to, so two objects cannot
//    collide;
//  * `data` is the array's memory, whose first element is the *lower left*
//    texel (ANARI's convention); the file is written top scanline first, as
//    OpenEXR expects;
//  * `channels` is how many channels the file gets (3 or 4);
//  * `mipmapped` asks for the full MIP pyramid. Shading textures need it (the
//    texture system refuses an unmipped file); an environment map does not,
//    because `EnvLight` reads the file itself, and a pyramid that reaches 1x1
//    makes it log "1x1 mip level not supported at present" for every map;
//  * sRGB element types are linearized, because the file is linear float;
//  * `replicateX`/`replicateY` write every texel as a block of that many
//    pixels. MoonRay filters textures through OpenImageIO, which has no
//    nearest-neighbor mode an ImageMap can select; a magnified copy of the
//    image keeps the texel edges sharp under that filtering, which is how
//    the samplers emulate filter = "nearest".
std::string writeScratchTexture(const char *tag,
    const void *owner,
    uint64_t width,
    uint64_t height,
    ANARIDataType type,
    const void *data,
    int channels,
    bool mipmapped,
    std::string &error,
    uint32_t replicateX = 1,
    uint32_t replicateY = 1);

// The replication factor that makes an image of `extent` texels (along one
// axis) about `target` pixels wide when filter = "nearest" is emulated: 1
// for images that are already that large.
uint32_t nearestReplication(uint64_t extent, uint64_t target = 1024);

// Best-effort removal of a file written by writeScratchTexture(). A leftover
// scratch file is harmless, so failures are ignored.
void removeScratchTexture(const std::string &path);

} // namespace texture
} // namespace moonray_anari
