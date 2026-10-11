// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "Transform.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Transform::Transform(MoonrayDeviceGlobalState *s) : Sampler(s) {}

void Transform::commitParameters()
{
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_output.set(getParam<mat4>("outTransform", mat4(linalg::identity)),
      getParam<float4>("outOffset", float4(0.f)));
}

void Transform::finalize()
{
  m_valid = primitiveAttribute(m_inAttribute, m_attribute, m_attributeType);
  if (!m_valid) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the transform sampler reads 'color' and 'attribute0' .. "
        "'attribute3'; '%s' is not available",
        m_inAttribute.c_str());
  }
}

bool Transform::isValid() const
{
  return m_valid;
}

const char *Transform::rdl2ClassName() const
{
  return "AttributeMap";
}

bool Transform::supports(SamplerUsage usage, std::string &why) const
{
  if (usage != SamplerUsage::Color) {
    why = "MoonRay can only scale a geometry attribute for a color parameter";
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (i != j && m_output.matrix[i][j] != 0.f) {
        why = "its 'outTransform' mixes channels, and MoonRay can only scale "
              "each channel of a geometry attribute";
        return false;
      }
    }
    if (m_output.offset[i] != 0.f) {
      why = "it has an 'outOffset', and MoonRay can only scale a geometry "
            "attribute";
      return false;
    }
  }
  return true;
}

void Transform::syncRdl2(scene_rdl2::rdl2::SceneObject &obj, SamplerUsage)
{
  // 0 == "primitive attribute".
  obj.set("map_type", 0);
  obj.set("primitive_attribute_name", std::string(m_attribute));
  obj.set("primitive_attribute_type", m_attributeType);
  // A geometry without the attribute renders black rather than
  // AttributeMap's white default, which would look like a working binding.
  obj.set("default_value", scene_rdl2::rdl2::Rgb(0.f, 0.f, 0.f));
  obj.set("warn_when_unavailable", true);
}

float3 Transform::attributeFactor(SamplerUsage) const
{
  return float3(
      m_output.matrix[0][0], m_output.matrix[1][1], m_output.matrix[2][2]);
}

} // namespace moonray_anari
