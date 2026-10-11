// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "texture/Texture.h"
// std
#include <string>

namespace moonray_anari {

// ANARI's `outTransform` / `outOffset` of a sampler: the value the sampler
// hands the material is
//
//     out = outTransform * rgba + outOffset
//
// with rgba the (linear) texel. The matrix is general - it scales, mixes
// channels, replicates alpha - and the result is not confined to [0, 1].
struct OutputTransform
{
  // out[i] = sum_j matrix[i][j] * rgba[j] + offset[i]
  float matrix[4][4]{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  float offset[4]{0, 0, 0, 0};

  // From ANARI's column-major matrix.
  void set(const mat4 &outTransform, const float4 &outOffset);
};

// How an ImageMap delivers the transformed value to a material attribute
// that uses it as `usage`.
//
// ImageMap can do part of the work itself, on the plain texture every
// sampler of the image shares: it has a per-channel gain and offset (its
// "gain_offset" color correction, applied to the filtered lookup without
// clamping) and it can return the texture's alpha as a grey. That covers
//
//  * a color whose transform scales and offsets each channel by itself -
//    which is what a brightness or a tint is, and also the decoding of a
//    normal map from [0, 1] to [-1, 1] (SamplerUsage::Normal), which is
//    composed on top of the sampler's own transform here;
//  * a scalar that is any combination of r, g and b: MoonRay takes a
//    scalar as the *average* of the map's rgb, so the combination's
//    coefficients, times three, go into the gain and its offset, times
//    three, into one channel;
//  * a color or a scalar taken from the alpha channel alone.
//
// Everything else - a color matrix that mixes channels, a scalar that
// mixes alpha with color - is baked into a texture of its own, which holds
// the transformed texels (texture::PixelTransform) and is shared by every
// sampler with the same image and the same transform.
struct ImageMapOutput
{
  // The texture to use: the plain one (four channels, no transform), or a
  // baked one of `channels` channels.
  bool baked{false};
  int channels{4};
  texture::PixelTransform pixels;

  // ImageMap attributes.
  bool alphaOnly{false};
  bool gainOffset{false};
  float gain[3]{1.f, 1.f, 1.f};
  float offset[3]{0.f, 0.f, 0.f};
};

ImageMapOutput planImageMapOutput(
    const OutputTransform &transform, SamplerUsage usage);

// Acquires the texture `output` asks for - `layout` says how the plain one
// is laid out (replication for filter = "nearest") - and writes it and the
// output attributes into the ImageMap `obj`. Returns the texture, which the
// caller has to keep for as long as the map may be sampled; null with
// `error` set when the image cannot be used (the map then has no texture).
texture::TexturePtr writeImageMapOutput(scene_rdl2::rdl2::SceneObject &obj,
    const char *tag,
    const texture::ImageSource &source,
    texture::TextureLayout layout,
    const ImageMapOutput &output,
    std::string &error);

} // namespace moonray_anari
