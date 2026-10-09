// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Directional.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <cmath>

namespace moonray_anari {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Any orthonormal complement of `z`; a directional light is rotationally
// symmetric about its axis, so the choice of x/y is arbitrary.
void orthonormalBasis(const float3 &z, float3 &x, float3 &y)
{
  const float3 helper = std::fabs(z.y) < 0.99f ? float3(0.f, 1.f, 0.f)
                                              : float3(1.f, 0.f, 0.f);
  x = linalg::normalize(linalg::cross(helper, z));
  y = linalg::cross(z, x);
}

} // namespace

Directional::Directional(MoonrayDeviceGlobalState *s) : Light(s) {}

void Directional::commitParameters()
{
  Light::commitParameters();
  m_direction =
      linalg::normalize(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  m_irradiance = getParam<float>("irradiance", 1.f);
  m_angularDiameter = getParam<float>("angularDiameter", 0.f);

  if (hasParam("radiance")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'radiance' is not supported on the directional light yet - "
        "'irradiance' is used instead");
  }
}

bool Directional::isValid() const
{
  return m_irradiance > 0.f && linalg::length(m_direction) > 0.f;
}

const char *Directional::rdl2ClassName() const
{
  return "DistantLight";
}

void Directional::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, const mat4 &instanceXfm) const
{
  using scene_rdl2::rdl2::Mat4d;
  using scene_rdl2::rdl2::Rgb;
  using scene_rdl2::rdl2::Vec4d;

  // The instance transform rotates the light; its translation is irrelevant
  // for a light at infinity.
  const float4 xformed =
      linalg::mul(instanceXfm, float4(m_direction.x, m_direction.y, m_direction.z, 0.f));
  const float3 direction =
      linalg::normalize(float3(xformed.x, xformed.y, xformed.z));

  // MoonRay's DistantLight carries a built-in 180-degree rotation about x, so
  // the direction light travels in is the *negative* local z axis of
  // node_xform (DistantLight::update(): mDirection = frame.getZ() with
  // frame = rotX180 * local2world).
  const float3 zAxis = -direction;
  float3 xAxis, yAxis;
  orthonormalBasis(zAxis, xAxis, yAxis);

  // RDL2 uses row vectors, so the basis vectors are the rows.
  const Mat4d xform(Vec4d(xAxis.x, xAxis.y, xAxis.z, 0.0),
      Vec4d(yAxis.x, yAxis.y, yAxis.z, 0.0),
      Vec4d(zAxis.x, zAxis.y, zAxis.z, 0.0),
      Vec4d(0.0, 0.0, 0.0, 1.0));

  obj.set("node_xform", xform);
  obj.set("color", Rgb(m_color.x, m_color.y, m_color.z));
  // normalized == true keeps the irradiance independent of the angular
  // extent, which is exactly ANARI's separation of 'irradiance' from
  // 'angularDiameter'.
  obj.set("normalized", true);
  obj.set("intensity", m_irradiance / kPi);
  obj.set("exposure", 0.f);
  if (m_angularDiameter > 0.f)
    obj.set("angular_extent", m_angularDiameter * 180.f / kPi);
  // ANARI lights are not visible geometry; what camera rays see where nothing
  // is hit is the renderer's 'background'.
  obj.set("visible_in_camera", 0);
}

} // namespace moonray_anari
