// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'physicallyBased' material -> MoonRay UsdPreviewSurface.
//
// The two are close relatives: ANARI's physicallyBased and USD's
// UsdPreviewSurface are both metallic-workflow models with the same core
// parameters, so baseColor/metallic/roughness/ior/opacity/emissive/occlusion/
// clearcoat map one to one. What UsdPreviewSurface has no counterpart for
// (sheen, iridescence, attenuation, specular tinting) is reported as
// unsupported rather than approximated - the exact list and the reasoning is
// in docs/ANARI_MOONRAY_MAPPING.md.
//
// Sampler-valued parameters are RDL2 bindings of the sampler's map to the
// attribute: baseColor, emissive, metallic, roughness, clearcoat,
// clearcoatRoughness, opacity and normal. `normal` is a normal map: the
// sampler returns the encoded color, the device decodes it to the tangent
// space vector (2 c - 1), and UsdPreviewSurface takes that in the frame
// ANARI describes - x along the surface's dP/ds, z along its normal. `ior` and `transmission` are
// constants only.
//
// Opacity from a sampler is a *cutout*: UsdPreviewSurface's presence is
// either there or not (opacity >= opacityThreshold), so alphaMode "mask"
// cuts at alphaCutoff, exactly, and "blend" cuts at 0.5, which keeps the
// shape of a leaf or a decal but not a soft edge.
struct PhysicallyBased : public Material
{
  PhysicallyBased(MoonrayDeviceGlobalState *s);
  ~PhysicallyBased() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const override;
  std::vector<SamplerBinding> samplerBindings() const override;
  std::vector<AttributeBinding> attributeBindings() const override;
  bool usesPresence() const override;
  Emitter emitter() const override;

 private:
  // Warns once per commit about a parameter this material cannot express.
  void reportUnsupported(const char *name) const;

  // The MoonRay primitive attribute 'baseColor' names, empty when it is not
  // attribute-valued.
  std::string m_baseColorAttribute;
  helium::IntrusivePtr<Sampler> m_baseColorSampler;
  helium::IntrusivePtr<Sampler> m_emissiveSampler;
  helium::IntrusivePtr<Sampler> m_metallicSampler;
  helium::IntrusivePtr<Sampler> m_roughnessSampler;
  helium::IntrusivePtr<Sampler> m_clearcoatSampler;
  helium::IntrusivePtr<Sampler> m_clearcoatRoughnessSampler;
  helium::IntrusivePtr<Sampler> m_opacitySampler;
  helium::IntrusivePtr<Sampler> m_normalSampler;
  float3 m_baseColor{1.f, 1.f, 1.f};
  float m_opacity{1.f};
  float m_metallic{1.f};
  float m_roughness{1.f};
  float3 m_emissive{0.f, 0.f, 0.f};
  // `emissionSampling`, as Emitter::sides.
  int m_emissionSides{0};
  float m_ior{1.5f};
  float m_transmission{0.f};
  float m_clearcoat{0.f};
  float m_clearcoatRoughness{0.f};
  float m_alphaCutoff{0.5f};
  std::string m_alphaMode{"opaque"};
};

} // namespace moonray_anari
