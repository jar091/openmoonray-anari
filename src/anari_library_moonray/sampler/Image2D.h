// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "OutputTransform.h"
#include "Sampler.h"
#include "array/Array2D.h"
#include "texture/Texture.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'image2D' sampler -> MoonRay ImageMap.
//
// MoonRay samples textures through OpenImageIO's texture system, and
// `ImageMap`'s `texture` parameter is a name in it. ANARI hands the device
// pixels in an ANARIArray2D; the device registers them with OpenImageIO under
// a name of their own and points the map at that (texture/Texture.h), so
// nothing is written to disk. The image gets a new name whenever its pixels
// change, which also keeps OpenImageIO's texture cache from serving stale
// ones, and is released when the last sampler using it goes away.
//
// One sampler is one ImageMap per *usage* (SamplerUsage): the output
// transform reaches a color, a scalar and a normal attribute in different
// ways (sampler/OutputTransform.h). The textures behind them are only made
// when a material actually uses the sampler that way.
struct Image2D : public Sampler
{
  Image2D(MoonrayDeviceGlobalState *s);
  ~Image2D() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(
      scene_rdl2::rdl2::SceneObject &obj, SamplerUsage usage) override;
  const char *inputPrimitiveAttribute() const override;
  int inputPrimitiveAttributeType() const override;

  // The image as a plain texture - every texel one pixel, whatever 'filter'
  // says, and no output transform - for users that only want the pixels: an
  // `rdla` material binds its image parameters by name and brings its own
  // filtering and wrapping. Shared with this sampler's own texture whenever
  // the two are the same. Returns null with `error` set when there is no
  // usable image.
  texture::TexturePtr plainTexture(std::string &error) const;

 private:
  helium::ChangeObserverPtr<Array2D> m_image;
  std::string m_inAttribute{"attribute0"};
  std::string m_filter{"linear"};
  std::string m_wrapMode1{"clampToEdge"};
  std::string m_wrapMode2{"clampToEdge"};
  OutputTransform m_output;

  // The primitive attribute the coordinates come from when it is not the
  // mesh's st (inAttribute other than attribute0), and its AttributeMap type.
  const char *m_inputAttribute{nullptr};
  int m_inputAttributeType{2};

  // The texture of each usage's map, and the ones the previous commit left:
  // those are kept until the next commit so that a usage that is synced
  // again finds its texture still alive (and shares it) instead of having
  // it built again.
  texture::TexturePtr m_textures[3];
  texture::TexturePtr m_previousTextures[3];

  // ImageMap's texture coordinate transform: st' = scale * R(-angle) * st +
  // offset, derived from 'inTransform'.
  float m_uvScale[2]{1.f, 1.f};
  float m_uvOffset[2]{0.f, 0.f};
  float m_uvRotationDegrees{0.f};
  bool m_valid{false};
};

} // namespace moonray_anari
