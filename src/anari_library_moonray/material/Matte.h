// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'matte' material -> MoonRay UsdPreviewSurface driven into its purely
// Lambertian configuration (metallic 0, roughness 1, ior 1 so the dielectric
// lobe's Fresnel reflectance is zero). See docs/ANARI_MOONRAY_MAPPING.md.
//
// 'color' takes a constant, an image2D sampler (an RDL2 binding from
// diffuseColor to the sampler's Map) or the name of a geometry attribute (a
// binding to an AttributeMap reading that primitive attribute). Attribute
// names other than "color" are reported as unsupported rather than silently
// ignored.
struct Matte : public Material
{
  Matte(MoonrayDeviceGlobalState *s);
  ~Matte() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const override;
  std::vector<SamplerBinding> samplerBindings() const override;
  std::vector<AttributeBinding> attributeBindings() const override;

 private:
  helium::IntrusivePtr<Sampler> m_colorSampler;
  // The MoonRay primitive attribute 'color' names, empty when it is not
  // attribute-valued.
  std::string m_colorAttribute;
  float3 m_color{0.8f, 0.8f, 0.8f};
  float m_opacity{1.f};
  float m_alphaCutoff{0.5f};
  std::string m_alphaMode{"opaque"};
};

} // namespace moonray_anari
