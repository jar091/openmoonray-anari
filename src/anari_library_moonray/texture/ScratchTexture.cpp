// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ScratchTexture.h"
#include "ScratchFile.h"

// anari
#include <anari/frontend/type_utility.h>
// OpenImageIO
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
// std
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace moonray_anari {
namespace texture {

namespace {

// Converts one ANARI texel to RGBA float. Unsupported element types return
// false rather than guessing.
bool texelToRgba(
    ANARIDataType type, const void *data, size_t index, float out[4])
{
  out[0] = out[1] = out[2] = 0.f;
  out[3] = 1.f;

  switch (type) {
  case ANARI_FLOAT32: {
    const auto *v = static_cast<const float *>(data) + index;
    out[0] = out[1] = out[2] = v[0];
    return true;
  }
  case ANARI_FLOAT32_VEC2: {
    const auto *v = static_cast<const float *>(data) + 2 * index;
    out[0] = v[0];
    out[1] = v[1];
    return true;
  }
  case ANARI_FLOAT32_VEC3: {
    const auto *v = static_cast<const float *>(data) + 3 * index;
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    return true;
  }
  case ANARI_FLOAT32_VEC4: {
    const auto *v = static_cast<const float *>(data) + 4 * index;
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    out[3] = v[3];
    return true;
  }
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_R_SRGB: {
    const auto *v = static_cast<const uint8_t *>(data) + index;
    out[0] = out[1] = out[2] = v[0] / 255.f;
    return true;
  }
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_RGB_SRGB: {
    const auto *v = static_cast<const uint8_t *>(data) + 3 * index;
    for (int c = 0; c < 3; ++c)
      out[c] = v[c] / 255.f;
    return true;
  }
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_RGBA_SRGB: {
    const auto *v = static_cast<const uint8_t *>(data) + 4 * index;
    for (int c = 0; c < 4; ++c)
      out[c] = v[c] / 255.f;
    return true;
  }
  default:
    return false;
  }
}

// sRGB-encoded element types have to be linearized here, because the file is
// written as linear float and MoonRay is told gamma is off.
bool isSrgbType(ANARIDataType type)
{
  return type == ANARI_UFIXED8_R_SRGB || type == ANARI_UFIXED8_RGB_SRGB
      || type == ANARI_UFIXED8_RGBA_SRGB;
}

float srgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

} // namespace

std::string writeScratchTexture(const char *tag,
    const void *owner,
    uint64_t width,
    uint64_t height,
    ANARIDataType type,
    const void *data,
    int channels,
    bool mipmapped,
    std::string &error,
    uint32_t replicateX,
    uint32_t replicateY)
{
  error.clear();

  if (width == 0 || height == 0) {
    error = "the image is empty";
    return {};
  }
  if (!data) {
    error = "the image has no data";
    return {};
  }
  channels = std::clamp(channels, 1, 4);
  replicateX = std::max(replicateX, 1u);
  replicateY = std::max(replicateY, 1u);
  const size_t outWidth = size_t(width) * replicateX;
  const size_t outHeight = size_t(height) * replicateY;

  std::vector<float> pixels(outWidth * outHeight * size_t(channels));
  const bool srgb = isSrgbType(type);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t src = y * size_t(width) + x;
      // ANARI images start at the lower left; OpenEXR scanline 0 is the top.
      const size_t dstRow = (size_t(height) - 1 - y) * replicateY;
      const size_t dstColumn = x * replicateX;
      float texel[4];
      if (!texelToRgba(type, data, src, texel)) {
        error = std::string("images of type ") + anari::toString(type)
            + " cannot be used as a texture";
        return {};
      }
      if (srgb) {
        for (int c = 0; c < 3; ++c)
          texel[c] = srgbToLinear(texel[c]);
      }
      for (uint32_t ry = 0; ry < replicateY; ++ry) {
        for (uint32_t rx = 0; rx < replicateX; ++rx) {
          const size_t dst = (dstRow + ry) * outWidth + dstColumn + rx;
          for (int c = 0; c < channels; ++c)
            pixels[size_t(channels) * dst + c] = texel[c];
        }
      }
    }
  }

  const std::string path = scratch::newFilePath(tag, owner, "exr", error);
  if (path.empty())
    return {};

  OIIO::ImageSpec spec(
      int(outWidth), int(outHeight), channels, OIIO::TypeDesc::FLOAT);
  OIIO::ImageBuf source(spec, pixels.data());

  OIIO::ImageSpec config;
  // Level 0 must stay exactly the pixels the application handed over: no
  // resizing to a power of two, no color conversion.
  config.attribute("maketx:resize", 0);
  config.attribute("maketx:filtername", "box");
  config.attribute("maketx:updatemode", 0);
  config.attribute("maketx:oiio_options", 1);
  config.attribute("maketx:fileformatname", "exr");
  // An environment map needs no pyramid at all: EnvLight opens the file
  // itself instead of going through the texture system, and a pyramid that
  // reaches 1x1 makes it log "1x1 mip level not supported at present" for
  // every map it opens.
  if (!mipmapped)
    config.attribute("maketx:nomipmap", 1);

  std::ostringstream makeTxLog;
  if (!OIIO::ImageBufAlgo::make_texture(OIIO::ImageBufAlgo::MakeTxTexture,
          source,
          path,
          config,
          &makeTxLog)) {
    error = "could not write the texture '" + path + "': " + makeTxLog.str();
    scratch::removeFile(path);
    return {};
  }

  return path;
}

uint32_t nearestReplication(uint64_t extent, uint64_t target)
{
  if (extent == 0 || extent >= target)
    return 1;
  return uint32_t(std::min<uint64_t>(target / extent, 256));
}

void removeScratchTexture(const std::string &path)
{
  scratch::removeFile(path);
}

} // namespace texture
} // namespace moonray_anari
