// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <string>
#include <vector>

namespace moonray_anari {
namespace texture {

// Textures MoonRay reads straight from the device's memory, without a file.
//
// MoonRay only ever names a texture: `ImageMap` (and every other map built on
// shading::BasicTexture) hands the name to OpenImageIO's *shared* texture
// system, and `EnvLight` hands it to OIIO::ImageInput::open(). Both ends are
// OpenImageIO, and OpenImageIO lets an application provide the reader:
//
//  * the image cache behind the texture system takes a file name together
//    with the function that creates its ImageInput (ImageCache::add_file), so
//    that name is never looked for on disk;
//  * a format declared with OIIO::declare_imageio_format() is found by file
//    extension, which is what serves ImageInput::open().
//
// The reader (MemoryTexture.cpp) looks the name up in a process-wide table
// and copies tiles or scanlines out of the pixels registered here.
//
// One obstacle is MoonRay's own: texture::TextureSampler::getHandle() returns
// "no texture" for any name std::filesystem::exists() does not confirm,
// before OpenImageIO is asked. A texture-system name is therefore spelled as
// a path that resolves to a directory which is always there (the system temp
// directory), with the unique part in components that cancel out - see
// textureSystemName() in the .cpp. Nothing is created under it.

// How MoonRay is going to open the texture; decides the name and the layout.
enum class MemoryTextureUse
{
  // OpenImageIO's shared texture system (ImageMap and the other maps). The
  // texture is tiled and carries a full MIP pyramid, because MoonRay runs the
  // texture system with `accept_untiled` and `accept_unmipped` off.
  TextureSystem,
  // OIIO::ImageInput::open() (EnvLight). One level of scanlines: EnvLight
  // warns about every 1x1 MIP level it finds.
  ImageInput
};

struct MemoryTextureDesc
{
  size_t width{0};
  size_t height{0};
  // Channels per pixel (1..4).
  int channels{4};
  // Store 16-bit half floats instead of 32-bit floats. Meant for images that
  // arrived with 8 bits per channel, which half represents with room to
  // spare. Never an 8-bit integer layout: MoonRay applies its `gamma` to
  // those, and the pixels registered here are already linear.
  bool halfFloat{false};
  MemoryTextureUse use{MemoryTextureUse::TextureSystem};
};

// Registers `pixels` (desc.channels floats per pixel, top scanline first, the
// order of an image file) and returns the name MoonRay has to be given as the
// texture's file name. Returns an empty string with `error` set on failure.
// The pixels are taken over (and released early when stored as half).
std::string registerMemoryTexture(const MemoryTextureDesc &desc,
    std::vector<float> &&pixels,
    std::string &error);

// Forgets a texture registered with registerMemoryTexture() and makes
// OpenImageIO drop what it cached of it. Names this function does not know
// are ignored.
void releaseMemoryTexture(const std::string &name);

// Whether `name` is a texture currently registered here.
bool isMemoryTexture(const std::string &name);

// How many textures are registered, and how many bytes of pixels they hold.
// For diagnostics and tests.
size_t memoryTextureCount();
size_t memoryTextureBytes();

} // namespace texture
} // namespace moonray_anari
