// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Pixels.h"
#include "Parallel.h"

// anari
#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cmath>
#include <mutex>

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

// sRGB-encoded element types have to be linearized here, because the texture
// is linear float and MoonRay is told gamma is off.
bool isSrgbType(ANARIDataType type)
{
  return type == ANARI_UFIXED8_R_SRGB || type == ANARI_UFIXED8_RGB_SRGB
      || type == ANARI_UFIXED8_RGBA_SRGB;
}

float srgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

// The sRGB element types are 8 bit, so the transfer function is a table of
// 256 values rather than a pow() per component of every texel.
const float *srgbTable()
{
  static const std::vector<float> table = [] {
    std::vector<float> t(256);
    for (int i = 0; i < 256; ++i)
      t[size_t(i)] = srgbToLinear(float(i) / 255.f);
    return t;
  }();
  return table.data();
}

} // namespace

bool convertPixels(uint64_t width,
    uint64_t height,
    ANARIDataType type,
    const void *data,
    int channels,
    uint32_t replicateX,
    uint32_t replicateY,
    std::vector<float> &pixels,
    size_t &outWidth,
    size_t &outHeight,
    std::string &error,
    const PixelTransform *transform,
    float *maxAbs)
{
  error.clear();
  if (transform && !transform->enabled)
    transform = nullptr;
  float largest = 0.f;

  if (width == 0 || height == 0) {
    error = "the image is empty";
    return false;
  }
  if (!data) {
    error = "the image has no data";
    return false;
  }
  channels = std::clamp(channels, 1, 4);
  replicateX = std::max(replicateX, 1u);
  replicateY = std::max(replicateY, 1u);
  outWidth = size_t(width) * replicateX;
  outHeight = size_t(height) * replicateY;

  {
    float probe[4];
    if (!texelToRgba(type, data, 0, probe)) {
      error = std::string("images of type ") + anari::toString(type)
          + " cannot be used as a texture";
      return false;
    }
  }

  pixels.resize(outWidth * outHeight * size_t(channels));
  const bool srgb = isSrgbType(type);
  const float *toLinear = srgb ? srgbTable() : nullptr;
  std::mutex largestMutex;
  // Rows are independent of each other; about 64k texels per thread make
  // starting one worthwhile.
  parallelRanges(size_t(height),
      std::max<size_t>(1, 65536 / size_t(width)),
      [&](size_t rowBegin, size_t rowEnd) {
  float largestHere = 0.f;
  for (size_t y = rowBegin; y < rowEnd; ++y) {
    // ANARI images start at the lower left; scanline 0 of a file is the top.
    const size_t dstRow = (size_t(height) - 1 - y) * replicateY;
    float *row = pixels.data() + dstRow * outWidth * size_t(channels);
    for (size_t x = 0; x < width; ++x) {
      const size_t src = y * size_t(width) + x;
      float texel[4];
      texelToRgba(type, data, src, texel);
      if (srgb) {
        // The components came from 8 bits, so they index the table exactly.
        for (int c = 0; c < 3; ++c)
          texel[c] = toLinear[int(texel[c] * 255.f + 0.5f)];
      }
      if (transform) {
        float out[4];
        for (int c = 0; c < channels; ++c) {
          const float *row = transform->matrix[c];
          out[c] = row[0] * texel[0] + row[1] * texel[1] + row[2] * texel[2]
              + row[3] * texel[3] + transform->offset[c];
          largestHere = std::max(largestHere, std::fabs(out[c]));
        }
        for (int c = 0; c < channels; ++c)
          texel[c] = out[c];
      }
      float *dst = row + x * replicateX * size_t(channels);
      for (uint32_t rx = 0; rx < replicateX; ++rx) {
        for (int c = 0; c < channels; ++c)
          dst[size_t(rx) * size_t(channels) + size_t(c)] = texel[c];
      }
    }
    // The remaining rows of the block are copies of the first.
    const size_t rowFloats = outWidth * size_t(channels);
    for (uint32_t ry = 1; ry < replicateY; ++ry)
      std::copy(row, row + rowFloats, row + size_t(ry) * rowFloats);
  }
  std::lock_guard<std::mutex> guard(largestMutex);
  largest = std::max(largest, largestHere);
      });

  if (maxAbs)
    *maxAbs = largest;
  return true;
}

bool isImageType(ANARIDataType type)
{
  const uint8_t zero[16] = {};
  float probe[4];
  return texelToRgba(type, zero, 0, probe);
}

bool isEightBit(ANARIDataType type)
{
  switch (type) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    return true;
  default:
    return false;
  }
}

} // namespace texture
} // namespace moonray_anari
