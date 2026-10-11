// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "Texture.h"
#include "MemoryTexture.h"
#include "Pixels.h"
#include "ScratchTexture.h"

// std
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace moonray_anari {
namespace texture {

namespace {

// Everything that makes two requests the same texture.
using Key = std::tuple<const void *, // array
    uint64_t, // version
    const void *, // data
    uint64_t, // width
    uint64_t, // height
    int, // element type
    int, // channels
    bool, // mipmapped
    uint32_t, // replicateX
    uint32_t, // replicateY
    std::array<float, 21>>; // transform: enabled, matrix, offset

struct Cache
{
  std::mutex mutex;
  std::map<Key, std::weak_ptr<const Texture>> textures;
};

// Never destroyed, like the registry of in-memory textures: the last
// textures may be released while the process is already shutting down.
Cache &cache()
{
  static Cache *c = new Cache;
  return *c;
}

} // namespace

Texture::Texture(std::string name, bool memory)
    : m_name(std::move(name)), m_memory(memory)
{}

Texture::~Texture()
{
  if (m_memory)
    releaseMemoryTexture(m_name);
  else
    removeScratchTexture(m_name);
}

bool texturesAreFiles()
{
  static const bool files = [] {
    const char *value = std::getenv("OPENMOONRAY_ANARI_TEXTURE_FILES");
    return value && *value && std::strcmp(value, "0") != 0;
  }();
  return files;
}

uint32_t nearestReplication(uint64_t extent, uint64_t target)
{
  if (extent == 0 || extent >= target)
    return 1;
  return uint32_t(std::min<uint64_t>(target / extent, 256));
}

TexturePtr acquireTexture(const char *tag,
    const ImageSource &source,
    const TextureLayout &layout,
    std::string &error)
{
  error.clear();

  std::array<float, 21> transform{};
  if (layout.transform.enabled) {
    transform[0] = 1.f;
    // Only the rows that end up in the texture tell two textures apart.
    for (int row = 0; row < std::clamp(layout.channels, 1, 4); ++row) {
      for (int column = 0; column < 4; ++column)
        transform[size_t(1 + 4 * row + column)] =
            layout.transform.matrix[row][column];
      transform[size_t(17 + row)] = layout.transform.offset[row];
    }
  }

  const Key key(source.array,
      source.version,
      source.data,
      source.width,
      source.height,
      int(source.type),
      layout.channels,
      layout.mipmapped,
      layout.replicateX,
      layout.replicateY,
      transform);

  Cache &c = cache();
  // Held across the conversion: two samplers finalizing the same image at
  // once would otherwise both build it.
  std::lock_guard<std::mutex> guard(c.mutex);

  auto found = c.textures.find(key);
  if (found != c.textures.end()) {
    if (TexturePtr texture = found->second.lock())
      return texture;
  }

  // A new texture: the moment to forget the ones that are gone.
  for (auto it = c.textures.begin(); it != c.textures.end();) {
    if (it->second.expired())
      it = c.textures.erase(it);
    else
      ++it;
  }

  std::vector<float> pixels;
  size_t width = 0;
  size_t height = 0;
  float maxAbs = 0.f;
  if (!convertPixels(source.width,
          source.height,
          source.type,
          source.data,
          layout.channels,
          layout.replicateX,
          layout.replicateY,
          pixels,
          width,
          height,
          error,
          &layout.transform,
          &maxAbs))
    return nullptr;

  std::string name;
  const bool memory = !texturesAreFiles();
  if (memory) {
    MemoryTextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.channels = layout.channels;
    // Half floats hold what came from 8 bits per channel, also after a
    // transform took it out of [0, 1] - up to the largest half, beyond which
    // the texture is stored as float rather than clamped.
    desc.halfFloat = isEightBit(source.type) && maxAbs <= 65000.f;
    desc.use = layout.mipmapped ? MemoryTextureUse::TextureSystem
                                : MemoryTextureUse::ImageInput;
    name = registerMemoryTexture(desc, std::move(pixels), error);
  } else {
    name = writeScratchTexture(tag,
        source.array,
        width,
        height,
        layout.channels,
        pixels.data(),
        layout.mipmapped,
        error);
  }
  if (name.empty())
    return nullptr;

  TexturePtr texture = std::make_shared<const Texture>(std::move(name), memory);
  c.textures[key] = texture;
  return texture;
}

} // namespace texture
} // namespace moonray_anari
