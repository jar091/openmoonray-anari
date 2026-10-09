// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PhysicallyBased.h"

// std
#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

PhysicallyBased::PhysicallyBased(MoonrayDeviceGlobalState *s) : Material(s) {}

void PhysicallyBased::reportUnsupported(const char *name) const
{
  if (!hasParam(name))
    return;

  // Applications commonly set every parameter, most of them to values which
  // have no effect (e.g. a transmission of 0, or a specular weight of 1 that
  // keeps the Fresnel reflectance of the IOR): only report parameters which
  // would change the result.
  if (hasParam(name, ANARI_FLOAT32)) {
    const float v = getParam<float>(name, 0.f);
    const bool isWeight = std::strcmp(name, "specular") == 0;
    if (v == 0.f || (isWeight && v == 1.f))
      return;
  } else if (hasParam(name, ANARI_FLOAT32_VEC3)) {
    const float3 v = getParam<float3>(name, float3(0.f, 0.f, 0.f));
    const bool isTint = std::strcmp(name, "specularColor") == 0
        || std::strcmp(name, "attenuationColor") == 0;
    if ((v.x == 0.f && v.y == 0.f && v.z == 0.f)
        || (isTint && v.x == 1.f && v.y == 1.f && v.z == 1.f))
      return;
  }

  // Once per parameter and process, it applies to every such material.
  static std::mutex mutex;
  static std::set<std::string> reported;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!reported.insert(name).second)
      return;
  }

  reportMessage(ANARI_SEVERITY_WARNING,
      "'%s' is not supported by the physicallyBased material: MoonRay's "
      "UsdPreviewSurface has no counterpart for it (see "
      "docs/ANARI_MOONRAY_MAPPING.md)",
      name);
}

void PhysicallyBased::commitParameters()
{
  m_baseColor = getParam<float3>("baseColor", float3(1.f, 1.f, 1.f));
  m_opacity = getParam<float>("opacity", 1.f);
  m_metallic = getParam<float>("metallic", 1.f);
  m_roughness = getParam<float>("roughness", 1.f);
  m_emissive = getParam<float3>("emissive", float3(0.f, 0.f, 0.f));
  m_ior = getParam<float>("ior", 1.5f);
  m_transmission = getParam<float>("transmission", 0.f);
  m_clearcoat = getParam<float>("clearcoat", 0.f);
  m_clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
  m_alphaMode = getParamString("alphaMode", "opaque");

  if (m_alphaMode != "opaque" && m_alphaMode != "blend"
      && m_alphaMode != "mask") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'alphaMode' \"%s\" on physicallyBased material - treating as "
        "opaque",
        m_alphaMode.c_str());
    m_alphaMode = "opaque";
  }

  // `baseColor` may name a geometry attribute instead of carrying a value.
  // MoonRay reads that through an AttributeMap bound to diffuseColor, and
  // since a bound map *multiplies* the attribute (shading::evalColor), the
  // constant becomes the identity.
  m_baseColorAttribute.clear();
  if (hasParam("baseColor", ANARI_STRING)) {
    const std::string attribute = getParamString("baseColor", "");
    if (attribute == "color") {
      m_baseColorAttribute = "Cd";
      m_baseColor = float3(1.f, 1.f, 1.f);
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'baseColor' = \"%s\": the physicallyBased material can only read "
          "the \"color\" geometry attribute - using the constant default",
          attribute.c_str());
    }
  }

  // An image2D sampler binds diffuseColor to the sampler's map, which
  // MoonRay multiplies by the attribute's own value: the constant becomes the
  // identity (see Matte::commitParameters()).
  m_baseColorSampler = getParamObject<Sampler>("baseColor");
  if (m_baseColorSampler)
    m_baseColor = float3(1.f, 1.f, 1.f);

  // The remaining sampler- and attribute-valued parameters are not supported;
  // saying so is better than quietly rendering a constant.
  for (const char *name : {"opacity",
           "metallic",
           "roughness",
           "emissive",
           "clearcoat",
           "clearcoatRoughness",
           "ior",
           "transmission"}) {
    if (hasParam(name, ANARI_SAMPLER) || hasParam(name, ANARI_STRING)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "sampler- and attribute-valued '%s' are not supported by the "
          "physicallyBased material yet - using the constant default",
          name);
    }
  }

  // Parameters UsdPreviewSurface cannot express at all.
  for (const char *name : {"normal",
           "occlusion",
           "clearcoatNormal",
           "specular",
           "specularColor",
           "thickness",
           "attenuationDistance",
           "attenuationColor",
           "sheenColor",
           "sheenRoughness",
           "iridescence",
           "iridescenceIor",
           "iridescenceThickness"}) {
    reportUnsupported(name);
  }
}

std::vector<Material::SamplerBinding> PhysicallyBased::samplerBindings() const
{
  if (m_baseColorSampler && m_baseColorSampler->isValid())
    return {{"diffuseColor", m_baseColorSampler.ptr}};
  return {};
}

bool PhysicallyBased::isValid() const
{
  return true;
}

const char *PhysicallyBased::rdl2ClassName() const
{
  return "UsdPreviewSurface";
}

void PhysicallyBased::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::Rgb;

  // alphaMode decides whether 'opacity' has any effect, exactly as for matte.
  float opacity = 1.f;
  if (m_alphaMode == "mask")
    opacity = m_opacity >= m_alphaCutoff ? 1.f : 0.f;
  else if (m_alphaMode == "blend")
    opacity = m_opacity;

  obj.set("diffuseColor", Rgb(m_baseColor.x, m_baseColor.y, m_baseColor.z));
  obj.set("emissiveColor", Rgb(m_emissive.x, m_emissive.y, m_emissive.z));
  // 0 == metalness workflow, which is the one ANARI describes.
  obj.set("useSpecularWorkflow", 0);
  obj.set("metallic", m_metallic);
  obj.set("roughness", m_roughness);
  obj.set("ior", m_ior);
  obj.set("clearcoat", m_clearcoat);
  // UsdPreviewSurface treats a clearcoat roughness of 0 as a mirror lobe,
  // which is also what ANARI's default 0 means.
  obj.set("clearcoatRoughness", m_clearcoatRoughness);
  // UsdPreviewSurface has no transmission input of its own: with
  // opacityThreshold 0 its 'opacity' is the dielectric lobe's *refraction*
  // weight (transmission = 1 - opacity, refracted through 'ior'; the diffuse
  // lobe below only gets what is not transmitted). That is exactly ANARI's
  // (glTF's) 'transmission', so it is folded in here; without it a clear
  // glass material rendered as an opaque white diffuse surface. The
  // transmitted light is untinted - UsdPreviewSurface fixes the
  // transmission color to white - and the dielectric lobe only exists while
  // metallic < 1, as in the metal/dielectric blend ANARI describes.
  const float transmission = std::clamp(m_transmission, 0.f, 1.f);
  obj.set("opacity", opacity * (1.f - transmission));
  obj.set("opacityThreshold", 0.f);
  obj.set("occlusion", 1.f);
  obj.set("displacement", 0.f);
}

std::vector<Material::AttributeBinding> PhysicallyBased::attributeBindings()
    const
{
  if (!m_baseColorAttribute.empty())
    return {{"diffuseColor", m_baseColorAttribute, /*rgb=*/3}};
  return {};
}

} // namespace moonray_anari
