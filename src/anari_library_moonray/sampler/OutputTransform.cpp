// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "OutputTransform.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

void OutputTransform::set(const mat4 &outTransform, const float4 &outOffset)
{
  // linalg matrices are indexed [column][row].
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column)
      matrix[row][column] = outTransform[column][row];
    offset[row] = outOffset[row];
  }
}

ImageMapOutput planImageMapOutput(
    const OutputTransform &transform, SamplerUsage usage)
{
  ImageMapOutput out;

  OutputTransform t = transform;
  if (usage == SamplerUsage::Normal) {
    // The sampler's output is the encoded normal; the vector the material
    // wants is 2 * output - 1, which is one more scale and offset on the
    // first three components.
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 4; ++j)
        t.matrix[i][j] *= 2.f;
      t.offset[i] = 2.f * t.offset[i] - 1.f;
    }
  }

  if (usage == SamplerUsage::Scalar) {
    // The first component of the sampler's output:
    //     value = c . rgb + a * alpha + o
    const float *c = t.matrix[0];
    const float a = t.matrix[0][3];
    const float o = t.offset[0];
    const bool usesColor = c[0] != 0.f || c[1] != 0.f || c[2] != 0.f;

    if (a == 0.f) {
      // MoonRay averages the map's rgb: three times the combination, with
      // the whole offset in the first channel, averages to the value.
      out.gainOffset = true;
      for (int i = 0; i < 3; ++i)
        out.gain[i] = 3.f * c[i];
      out.offset[0] = 3.f * o;
    } else if (!usesColor) {
      // The alpha as a grey, scaled and offset; the average of a grey is
      // the grey.
      out.alphaOnly = true;
      out.gainOffset = a != 1.f || o != 0.f;
      for (int i = 0; i < 3; ++i) {
        out.gain[i] = a;
        out.offset[i] = o;
      }
    } else {
      // Color and alpha together: a one-channel texture of the value, which
      // MoonRay shows as a grey.
      out.baked = true;
      out.channels = 1;
      out.pixels.enabled = true;
      for (int j = 0; j < 4; ++j)
        out.pixels.matrix[0][j] = t.matrix[0][j];
      out.pixels.offset[0] = o;
    }
    return out;
  }

  // Color and Normal: the first three components.
  bool diagonal = true;
  bool usesColor = false;
  bool usesAlpha = false;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      if (t.matrix[i][j] != 0.f) {
        usesColor = true;
        if (i != j)
          diagonal = false;
      }
    }
    usesAlpha = usesAlpha || t.matrix[i][3] != 0.f;
  }

  if (diagonal && !usesAlpha) {
    for (int i = 0; i < 3; ++i) {
      out.gain[i] = t.matrix[i][i];
      out.offset[i] = t.offset[i];
      out.gainOffset =
          out.gainOffset || out.gain[i] != 1.f || out.offset[i] != 0.f;
    }
  } else if (!usesColor) {
    out.alphaOnly = true;
    for (int i = 0; i < 3; ++i) {
      out.gain[i] = t.matrix[i][3];
      out.offset[i] = t.offset[i];
      out.gainOffset =
          out.gainOffset || out.gain[i] != 1.f || out.offset[i] != 0.f;
    }
  } else {
    out.baked = true;
    out.channels = 3;
    out.pixels.enabled = true;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 4; ++j)
        out.pixels.matrix[i][j] = t.matrix[i][j];
      out.pixels.offset[i] = t.offset[i];
    }
  }
  return out;
}

texture::TexturePtr writeImageMapOutput(scene_rdl2::rdl2::SceneObject &obj,
    const char *tag,
    const texture::ImageSource &source,
    texture::TextureLayout layout,
    const ImageMapOutput &output,
    std::string &error)
{
  using scene_rdl2::rdl2::Rgb;

  layout.channels = output.baked ? output.channels : 4;
  layout.mipmapped = true;
  layout.transform = output.pixels;
  texture::TexturePtr texture =
      texture::acquireTexture(tag, source, layout, error);

  obj.set("texture", texture ? texture->name() : std::string());
  // The texture is linear float, so no decoding on top of it.
  obj.set("gamma", 0);
  obj.set("alpha_only", output.alphaOnly);
  obj.set("gain_offset_enabled", output.gainOffset);
  obj.set("gain", Rgb(output.gain[0], output.gain[1], output.gain[2]));
  obj.set("offset_adjust",
      Rgb(output.offset[0], output.offset[1], output.offset[2]));
  return texture;
}

} // namespace moonray_anari
