// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"

// std
#include <mutex>

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Matte::Matte(MoonrayDeviceGlobalState *s) : Material(s) {}

void Matte::commitParameters()
{
  m_colorSampler = getParamObject<Sampler>("color");
  m_colorAttribute.clear();
  if (m_colorSampler) {
    // The constant is not used; what the attribute holds while the sampler's
    // map is bound to it is decided in syncRdl2().
    m_color = float3(0.8f, 0.8f, 0.8f);
  } else if (hasParam("color", ANARI_STRING)) {
    const std::string attribute = getParamString("color", "");
    if (attribute == "color") {
      // The geometry writes ANARI's vertex.color as MoonRay's own vertex
      // color attribute; the same multiply-by-the-attribute rule applies, so
      // the constant is the identity again.
      m_colorAttribute = "Cd";
      m_color = float3(1.f, 1.f, 1.f);
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'color' = \"%s\": the matte material can only read the \"color\" "
          "geometry attribute - using the constant default",
          attribute.c_str());
      m_color = float3(0.8f, 0.8f, 0.8f);
    }
  } else if (hasParam("color")
      && !getParam("color", ANARI_FLOAT32_VEC3, &m_color)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'color' has a type the matte material cannot use - using the "
        "constant default");
    m_color = float3(0.8f, 0.8f, 0.8f);
  } else if (!hasParam("color")) {
    m_color = float3(0.8f, 0.8f, 0.8f);
  }

  m_opacitySampler = getParamObject<Sampler>("opacity");
  if (m_opacitySampler) {
    m_opacity = 1.f;
  } else if (hasParam("opacity")
      && !getParam("opacity", ANARI_FLOAT32, &m_opacity)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "attribute-valued 'opacity' is not supported by the matte material - "
        "using the constant default");
    m_opacity = 1.f;
  } else if (!hasParam("opacity")) {
    m_opacity = 1.f;
  }

  m_alphaMode = getParamString("alphaMode", "opaque");
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);

  if (m_alphaMode != "opaque" && m_alphaMode != "blend"
      && m_alphaMode != "mask") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'alphaMode' \"%s\" on matte material - treating as opaque",
        m_alphaMode.c_str());
    m_alphaMode = "opaque";
  }
}

void Matte::finalize()
{
  reportUnusable("color", m_colorSampler, SamplerUsage::Color);
  reportUnusable("opacity", m_opacitySampler, SamplerUsage::Scalar);

  if (m_alphaMode == "blend"
      && usable(m_opacitySampler, SamplerUsage::Scalar)) {
    // Once per process: it applies to every such material.
    static std::once_flag blendOnce;
    std::call_once(blendOnce, [this] {
      reportMessage(ANARI_SEVERITY_WARNING,
          "a sampler-valued 'opacity' with alphaMode \"blend\" is rendered as "
          "a cutout at 0.5: MoonRay's UsdPreviewSurface is either present or "
          "not at a point, it has no partial presence");
    });
  }
}

bool Matte::isValid() const
{
  return true;
}

const char *Matte::rdl2ClassName() const
{
  return "UsdPreviewSurface";
}

void Matte::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::Rgb;

  // ANARI alphaMode decides whether 'opacity' has any effect at all:
  // "opaque" ignores it, "mask" makes it a binary cutoff at alphaCutoff, and
  // "blend" passes it through.
  float opacity = 1.f;
  float opacityThreshold = 0.f;
  if (Sampler *s = m_alphaMode != "opaque"
          ? usable(m_opacitySampler, SamplerUsage::Scalar)
          : nullptr) {
    // From a map the opacity is a cutout: present where the map's value
    // reaches the threshold (see PhysicallyBased::syncRdl2()).
    opacity = s->attributeFactor(SamplerUsage::Scalar).x;
    opacityThreshold = m_alphaMode == "mask" ? m_alphaCutoff : 0.5f;
  } else if (m_alphaMode == "mask") {
    opacity = m_opacity >= m_alphaCutoff ? 1.f : 0.f;
  } else if (m_alphaMode == "blend") {
    opacity = m_opacity;
  }

  // MoonRay *multiplies* a bound map by the attribute's own value
  // (shading::evalColor), and skips the map entirely when the attribute is
  // black. ANARI's semantics are that the sampler *is* the value, so an
  // attribute with a map bound to it holds the identity - or the scale a
  // `transform` sampler leaves to it (Sampler::attributeFactor()).
  float3 color = m_color;
  if (Sampler *s = usable(m_colorSampler, SamplerUsage::Color))
    color = s->attributeFactor(SamplerUsage::Color);

  obj.set("diffuseColor", Rgb(color.x, color.y, color.z));
  obj.set("emissiveColor", Rgb(0.f, 0.f, 0.f));
  obj.set("useSpecularWorkflow", 0);
  obj.set("metallic", 0.f);
  obj.set("roughness", 1.f);
  obj.set("clearcoat", 0.f);
  // ior 1 gives the dielectric lobe zero Fresnel reflectance, leaving the
  // Lambertian diffuse lobe alone - which is what ANARI 'matte' is.
  obj.set("ior", 1.f);
  obj.set("opacity", opacity);
  obj.set("opacityThreshold", opacityThreshold);
  obj.set("occlusion", 1.f);
  obj.set("displacement", 0.f);
}

std::vector<Material::SamplerBinding> Matte::samplerBindings() const
{
  std::vector<SamplerBinding> bindings;
  if (Sampler *s = usable(m_colorSampler, SamplerUsage::Color))
    bindings.push_back({"diffuseColor", s, SamplerUsage::Color});
  if (m_alphaMode != "opaque") {
    if (Sampler *s = usable(m_opacitySampler, SamplerUsage::Scalar))
      bindings.push_back({"opacity", s, SamplerUsage::Scalar});
  }
  return bindings;
}

bool Matte::usesPresence() const
{
  return m_alphaMode != "opaque"
      && usable(m_opacitySampler, SamplerUsage::Scalar) != nullptr;
}

std::vector<Material::AttributeBinding> Matte::attributeBindings() const
{
  if (!m_colorAttribute.empty())
    return {{"diffuseColor", m_colorAttribute, /*rgb=*/3}};
  return {};
}

} // namespace moonray_anari
