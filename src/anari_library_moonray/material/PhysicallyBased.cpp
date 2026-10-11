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
  m_emissionSides = emissionSides();
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
  // MoonRay reads that through an AttributeMap bound to diffuseColor.
  m_baseColorAttribute.clear();
  if (hasParam("baseColor", ANARI_STRING)) {
    const std::string attribute = getParamString("baseColor", "");
    if (attribute == "color") {
      m_baseColorAttribute = "Cd";
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'baseColor' = \"%s\": the physicallyBased material can only read "
          "the \"color\" geometry attribute - using the constant default",
          attribute.c_str());
    }
  }

  // A sampler binds the attribute to the sampler's map; which of them can
  // actually be used is decided when the scene is synced (usable()).
  m_baseColorSampler = getParamObject<Sampler>("baseColor");
  m_emissiveSampler = getParamObject<Sampler>("emissive");
  m_metallicSampler = getParamObject<Sampler>("metallic");
  m_roughnessSampler = getParamObject<Sampler>("roughness");
  m_clearcoatSampler = getParamObject<Sampler>("clearcoat");
  m_clearcoatRoughnessSampler = getParamObject<Sampler>("clearcoatRoughness");
  m_opacitySampler = getParamObject<Sampler>("opacity");
  m_normalSampler = getParamObject<Sampler>("normal");

  // What is still a constant only; saying so is better than quietly
  // rendering one.
  for (const char *name : {"ior", "transmission"}) {
    if (hasParam(name, ANARI_SAMPLER) || hasParam(name, ANARI_STRING)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "sampler- and attribute-valued '%s' are not supported by the "
          "physicallyBased material - using the constant default",
          name);
    }
  }
  for (const char *name : {"opacity",
           "metallic",
           "roughness",
           "emissive",
           "clearcoat",
           "clearcoatRoughness",
           "normal"}) {
    if (hasParam(name, ANARI_STRING)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "attribute-valued '%s' is not supported by the physicallyBased "
          "material - using the constant default",
          name);
    }
  }

  // Parameters UsdPreviewSurface cannot express at all.
  for (const char *name : {"occlusion",
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

void PhysicallyBased::finalize()
{
  reportUnusable("baseColor", m_baseColorSampler, SamplerUsage::Color);
  reportUnusable("emissive", m_emissiveSampler, SamplerUsage::Color);
  reportUnusable("metallic", m_metallicSampler, SamplerUsage::Scalar);
  reportUnusable("roughness", m_roughnessSampler, SamplerUsage::Scalar);
  reportUnusable("clearcoat", m_clearcoatSampler, SamplerUsage::Scalar);
  reportUnusable(
      "clearcoatRoughness", m_clearcoatRoughnessSampler, SamplerUsage::Scalar);
  reportUnusable("opacity", m_opacitySampler, SamplerUsage::Scalar);
  reportUnusable("normal", m_normalSampler, SamplerUsage::Normal);

  if (usable(m_opacitySampler, SamplerUsage::Scalar)
      && m_alphaMode != "opaque") {
    // Once per process: it applies to every such material.
    static std::once_flag blendOnce;
    if (m_alphaMode == "blend") {
      std::call_once(blendOnce, [this] {
        reportMessage(ANARI_SEVERITY_WARNING,
            "a sampler-valued 'opacity' with alphaMode \"blend\" is rendered "
            "as a cutout at 0.5: MoonRay's UsdPreviewSurface is either "
            "present or not at a point, it has no partial presence");
      });
    }
    if (m_transmission > 0.f) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'transmission' has no effect together with a sampler-valued "
          "'opacity': UsdPreviewSurface uses its one opacity input either as "
          "the cutout or as the refraction weight");
    }
  }
}

std::vector<Material::SamplerBinding> PhysicallyBased::samplerBindings() const
{
  std::vector<SamplerBinding> bindings;
  auto add = [&](const char *attribute,
                 const helium::IntrusivePtr<Sampler> &sampler,
                 SamplerUsage usage) {
    if (Sampler *s = usable(sampler, usage))
      bindings.push_back({attribute, s, usage});
  };
  add("diffuseColor", m_baseColorSampler, SamplerUsage::Color);
  add("emissiveColor", m_emissiveSampler, SamplerUsage::Color);
  add("metallic", m_metallicSampler, SamplerUsage::Scalar);
  add("roughness", m_roughnessSampler, SamplerUsage::Scalar);
  add("clearcoat", m_clearcoatSampler, SamplerUsage::Scalar);
  add("clearcoatRoughness", m_clearcoatRoughnessSampler, SamplerUsage::Scalar);
  // alphaMode "opaque" ignores the opacity, textured or not.
  if (m_alphaMode != "opaque")
    add("opacity", m_opacitySampler, SamplerUsage::Scalar);
  add("normal", m_normalSampler, SamplerUsage::Normal);
  return bindings;
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

  // MoonRay *multiplies* a bound map by the attribute's own value
  // (shading::evalColor/evalFloat) and skips the map when that value is
  // zero. ANARI's semantics are that the sampler *is* the value, so an
  // attribute with a map bound to it holds what the sampler says the
  // identity is (Sampler::attributeFactor(): one, or the scale a `transform`
  // sampler cannot apply itself). The same rule covers an attribute-valued
  // baseColor, which is an AttributeMap bound by MoonrayScene.
  auto color = [](const helium::IntrusivePtr<Sampler> &sampler,
                   const float3 &constant) {
    Sampler *s = usable(sampler, SamplerUsage::Color);
    const float3 c = s ? s->attributeFactor(SamplerUsage::Color) : constant;
    return Rgb(c.x, c.y, c.z);
  };
  auto scalar = [](const helium::IntrusivePtr<Sampler> &sampler,
                    float constant) {
    Sampler *s = usable(sampler, SamplerUsage::Scalar);
    return s ? s->attributeFactor(SamplerUsage::Scalar).x : constant;
  };

  obj.set("diffuseColor",
      color(m_baseColorSampler,
          m_baseColorAttribute.empty() ? m_baseColor
                                       : float3(1.f, 1.f, 1.f)));
  obj.set("emissiveColor", color(m_emissiveSampler, m_emissive));
  // 0 == metalness workflow, which is the one ANARI describes.
  obj.set("useSpecularWorkflow", 0);
  obj.set("metallic", scalar(m_metallicSampler, m_metallic));
  obj.set("roughness", scalar(m_roughnessSampler, m_roughness));
  obj.set("ior", m_ior);
  obj.set("clearcoat", scalar(m_clearcoatSampler, m_clearcoat));
  // UsdPreviewSurface treats a clearcoat roughness of 0 as a mirror lobe,
  // which is also what ANARI's default 0 means.
  obj.set("clearcoatRoughness",
      scalar(m_clearcoatRoughnessSampler, m_clearcoatRoughness));
  // The tangent space normal: straight up unless a map is bound, which
  // UsdPreviewSurface then takes as it is (nothing is multiplied). The map
  // of a `normal` sampler decodes the normal map's color itself
  // (SamplerUsage::Normal).
  obj.set("normal", scene_rdl2::rdl2::Vec3f(0.f, 0.f, 1.f));
  obj.set("occlusion", 1.f);
  obj.set("displacement", 0.f);

  if (m_alphaMode != "opaque"
      && usable(m_opacitySampler, SamplerUsage::Scalar)) {
    // Opacity from a map is a cutout: UsdPreviewSurface is present where
    // opacity >= opacityThreshold, and with a threshold above zero the
    // opacity is no longer a refraction weight. "mask" cuts where ANARI
    // says; "blend" has no exact counterpart (see the header) and cuts in
    // the middle.
    obj.set("opacity", scalar(m_opacitySampler, 1.f));
    obj.set("opacityThreshold", m_alphaMode == "mask" ? m_alphaCutoff : 0.5f);
    return;
  }

  // alphaMode decides whether 'opacity' has any effect, exactly as for matte.
  float opacity = 1.f;
  if (m_alphaMode == "mask")
    opacity = m_opacity >= m_alphaCutoff ? 1.f : 0.f;
  else if (m_alphaMode == "blend")
    opacity = m_opacity;
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
}

bool PhysicallyBased::usesPresence() const
{
  return m_alphaMode != "opaque"
      && usable(m_opacitySampler, SamplerUsage::Scalar) != nullptr;
}

Material::Emitter PhysicallyBased::emitter() const
{
  Emitter emitter;
  Sampler *sampler = usable(m_emissiveSampler, SamplerUsage::Color);
  // What syncRdl2() writes: with a sampler the factor of its map.
  emitter.color =
      sampler ? sampler->attributeFactor(SamplerUsage::Color) : m_emissive;
  const bool emits =
      emitter.color.x > 0.f || emitter.color.y > 0.f || emitter.color.z > 0.f;
  emitter.sides = emits ? m_emissionSides : 0;
  emitter.rdl2Attribute = "emissiveColor";
  return emitter;
}

std::vector<Material::AttributeBinding> PhysicallyBased::attributeBindings()
    const
{
  if (!m_baseColorAttribute.empty())
    return {{"diffuseColor", m_baseColorAttribute, /*rgb=*/3}};
  return {};
}

} // namespace moonray_anari
