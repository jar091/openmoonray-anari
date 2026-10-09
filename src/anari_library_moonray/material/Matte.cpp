// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"

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
    // MoonRay *multiplies* a bound map by the attribute's own value
    // (shading::evalColor), and skips the map entirely when the attribute is
    // black. ANARI's semantics are that the sampler *is* the value, so a
    // textured attribute gets the identity - white - written into it.
    m_color = float3(1.f, 1.f, 1.f);
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

  if (hasParam("opacity")
      && !getParam("opacity", ANARI_FLOAT32, &m_opacity)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "sampler- and attribute-valued 'opacity' are not supported by the "
        "matte material yet - using the constant default");
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
  if (m_alphaMode == "mask")
    opacity = m_opacity >= m_alphaCutoff ? 1.f : 0.f;
  else if (m_alphaMode == "blend")
    opacity = m_opacity;

  obj.set("diffuseColor", Rgb(m_color.x, m_color.y, m_color.z));
  obj.set("emissiveColor", Rgb(0.f, 0.f, 0.f));
  obj.set("useSpecularWorkflow", 0);
  obj.set("metallic", 0.f);
  obj.set("roughness", 1.f);
  obj.set("clearcoat", 0.f);
  // ior 1 gives the dielectric lobe zero Fresnel reflectance, leaving the
  // Lambertian diffuse lobe alone - which is what ANARI 'matte' is.
  obj.set("ior", 1.f);
  obj.set("opacity", opacity);
  obj.set("opacityThreshold", 0.f);
  obj.set("occlusion", 1.f);
  obj.set("displacement", 0.f);
}

std::vector<Material::SamplerBinding> Matte::samplerBindings() const
{
  if (m_colorSampler && m_colorSampler->isValid())
    return {{"diffuseColor", m_colorSampler.ptr}};
  return {};
}

std::vector<Material::AttributeBinding> Matte::attributeBindings() const
{
  if (!m_colorAttribute.empty())
    return {{"diffuseColor", m_colorAttribute, /*rgb=*/3}};
  return {};
}

} // namespace moonray_anari
