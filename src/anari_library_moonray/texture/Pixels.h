// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari.h>
// std
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace moonray_anari {
namespace texture {

// An affine map of a texel's RGBA value, for textures that carry a sampler's
// output transform:
//
//     out[i] = matrix[i][0] * r + matrix[i][1] * g + matrix[i][2] * b
//            + matrix[i][3] * a + offset[i]
//
// It is applied to the linear value, i.e. after sRGB decoding. Texture
// filtering is linear as well, so transforming the texels and transforming
// the filtered lookup are the same thing.
struct PixelTransform
{
  bool enabled{false};
  float matrix[4][4]{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  float offset[4]{0, 0, 0, 0};
};

// Converts one ANARI 2D image into the float pixels MoonRay's textures are
// made of, in the order of an image file:
//
//  * `data` is the array's memory, whose first element is the *lower left*
//    texel (ANARI's convention); `pixels` starts with the top scanline;
//  * `channels` is how many channels every pixel gets (1..4). A one-component
//    element is grey, missing components are zero and a missing alpha is one;
//  * sRGB element types are linearized, because MoonRay is told the texture
//    is linear;
//  * `replicateX`/`replicateY` write every texel as a block of that many
//    pixels. MoonRay filters textures through OpenImageIO, which has no
//    nearest-neighbor mode an ImageMap can select; a magnified copy of the
//    image keeps the texel edges sharp under that filtering, which is how
//    the samplers emulate filter = "nearest";
//  * `transform`, when enabled, maps every texel; the pixel then holds the
//    first `channels` components of the result. `maxAbs` receives the
//    largest magnitude that was stored.
//
// Returns false with `error` set for an element type that is not an image.
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
    const PixelTransform *transform = nullptr,
    float *maxAbs = nullptr);

// Whether images of this element type can become a texture at all.
bool isImageType(ANARIDataType type);

// Whether the element type carries 8 bits per component, i.e. whether 16-bit
// half floats hold it without loss that matters.
bool isEightBit(ANARIDataType type);

} // namespace texture
} // namespace moonray_anari
