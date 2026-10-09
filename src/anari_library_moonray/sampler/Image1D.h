// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array1D.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'image1D' sampler -> MoonRay ImageMap reading an N x 1 texture.
//
// A 1D sampler is a color map looked up by a geometry attribute - typically a
// scalar per sphere or per vertex - so, unlike image2D, it cannot rely on the
// surface's st (spheres and curves have none that is the attribute). The map
// therefore samples its 'input texture coordinates', which MoonrayScene binds
// to an AttributeMap reading the attribute the geometry exported under
// Geometry::attribute0Name. The image is written to a scratch texture exactly
// as for image2D (texture/ScratchTexture.h).
struct Image1D : public Sampler
{
  Image1D(MoonrayDeviceGlobalState *s);
  ~Image1D() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) override;
  const char *inputPrimitiveAttribute() const override;

 private:
  bool writeTextureFile();

  helium::ChangeObserverPtr<Array1D> m_image;
  std::string m_inAttribute{"attribute0"};
  std::string m_filter{"linear"};
  std::string m_wrapMode{"clampToEdge"};

  std::string m_texturePath;
  // The 1D part of 'inTransform'/'inOffset': u' = scale * u + offset.
  float m_uScale{1.f};
  float m_uOffset{0.f};
  bool m_valid{false};
};

} // namespace moonray_anari
