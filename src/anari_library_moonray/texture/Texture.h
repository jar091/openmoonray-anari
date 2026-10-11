// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari.h>

#include "Pixels.h"
// std
#include <cstdint>
#include <memory>
#include <string>

namespace moonray_anari {
namespace texture {

// What the device hands MoonRay for the pixels of an ANARI array.
//
// MoonRay only ever takes a texture *name* (`ImageMap` and `EnvLight` have a
// `texture` attribute and nothing else). By default the name refers to
// pixels held in memory and served through OpenImageIO - MemoryTexture.h - so
// nothing is written to disk. Setting the environment variable
// OPENMOONRAY_ANARI_TEXTURE_FILES to anything but 0 goes back to scratch
// texture files (ScratchTexture.h): a real file is what `moonray -in` needs
// to render a scene dumped with OPENMOONRAY_ANARI_DUMP_RDLA, and it is the
// fallback should the in-memory route misbehave with some OpenImageIO.
//
// A Texture owns its pixels (or its file) and gives them back when the last
// reference goes away.
class Texture
{
 public:
  Texture(std::string name, bool memory);
  ~Texture();

  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;

  // The value for MoonRay's `texture` attributes.
  const std::string &name() const
  {
    return m_name;
  }

 private:
  std::string m_name;
  bool m_memory{true};
};

using TexturePtr = std::shared_ptr<const Texture>;

// The ANARI array the pixels come from.
struct ImageSource
{
  // Identifies the array and the state of its contents (the array object and
  // its helium `lastDataModified()`): two requests for the same image with
  // the same layout share one Texture, so an image used by many samplers and
  // materials is held once.
  const void *array{nullptr};
  uint64_t version{0};

  uint64_t width{0};
  uint64_t height{0};
  ANARIDataType type{ANARI_UNKNOWN};
  // First element = lower left texel.
  const void *data{nullptr};
};

struct TextureLayout
{
  // Channels of the texture (1 to 4); see convertPixels() for how the
  // array's elements fill them. MoonRay shows a one-channel texture as grey.
  int channels{4};
  // A tiled texture with a MIP pyramid, for the texture system (every map),
  // rather than one plain level, for EnvLight - which reads the image itself
  // and warns about every 1x1 MIP level it finds.
  bool mipmapped{true};
  // Each texel becomes a block of this many pixels; see convertPixels().
  uint32_t replicateX{1};
  uint32_t replicateY{1};
  // An output transform baked into the texels; see PixelTransform. Textures
  // of the same image with the same transform are shared like any other.
  PixelTransform transform;
};

// Returns the texture for `source` in `layout`, creating it when no live one
// matches. `tag` names the kind of texture ("image2d", "hdri", ...) for
// diagnostics. Returns null with `error` set when the image cannot be used.
// Thread safe.
TexturePtr acquireTexture(const char *tag,
    const ImageSource &source,
    const TextureLayout &layout,
    std::string &error);

// The replication factor that makes an image of `extent` texels (along one
// axis) about `target` pixels wide when filter = "nearest" is emulated: 1
// for images that are already that large.
uint32_t nearestReplication(uint64_t extent, uint64_t target = 1024);

// Whether textures are written to scratch files instead of being served
// from memory (OPENMOONRAY_ANARI_TEXTURE_FILES).
bool texturesAreFiles();

} // namespace texture
} // namespace moonray_anari
