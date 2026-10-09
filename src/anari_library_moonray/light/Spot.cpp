// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Spot.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <algorithm>
#include <cmath>

namespace moonray_anari {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// What a "radius 0" spot light becomes, see Point.cpp.
constexpr float kMinimumRadius = 1e-3f;

float toDegrees(float radians)
{
  return radians * (180.f / kPi);
}

} // namespace

Spot::Spot(MoonrayDeviceGlobalState *s) : Light(s) {}

void Spot::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_direction = getParam<float3>("direction", float3(0.f, 0.f, -1.f));
  m_intensity = getParam<float>("intensity", 1.f);
  m_openingAngle = getParam<float>("openingAngle", kPi);
  m_falloffAngle = getParam<float>("falloffAngle", 0.1f);
  m_radius = getParam<float>("radius", 0.f);

  if (hasParam("power")) {
    // Power is the flux into the cone: I = P / solid angle of the cone.
    const float solidAngle =
        2.f * kPi * (1.f - std::cos(0.5f * m_openingAngle));
    m_intensity = getParam<float>("power", 0.f) / std::max(solidAngle, 1e-6f);
  }

  if (linalg::length(m_direction) <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'direction' of the spot light must not be zero - using (0, 0, -1)");
    m_direction = float3(0.f, 0.f, -1.f);
  }
  m_openingAngle = std::clamp(m_openingAngle, 0.f, kPi);
  m_falloffAngle = std::clamp(m_falloffAngle, 0.f, 0.5f * m_openingAngle);
  m_radius = std::max(m_radius, 0.f);
}

bool Spot::isValid() const
{
  return m_intensity > 0.f && m_openingAngle > 0.f;
}

const char *Spot::rdl2ClassName() const
{
  return "SpotLight";
}

void Spot::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, const mat4 &instanceXfm) const
{
  using scene_rdl2::rdl2::Mat4d;
  using scene_rdl2::rdl2::Rgb;
  using scene_rdl2::rdl2::Vec4d;

  const float4 p = linalg::mul(
      instanceXfm, float4(m_position.x, m_position.y, m_position.z, 1.f));
  const float4 d = linalg::mul(
      instanceXfm, float4(m_direction.x, m_direction.y, m_direction.z, 0.f));

  // MoonRay lights emit along the negative z axis of node_xform.
  const float3 zAxis = -linalg::normalize(float3(d.x, d.y, d.z));
  const float3 helper = std::fabs(zAxis.z) < 0.999f ? float3(0.f, 0.f, 1.f)
                                                     : float3(1.f, 0.f, 0.f);
  const float3 xAxis = linalg::normalize(linalg::cross(helper, zAxis));
  const float3 yAxis = linalg::cross(zAxis, xAxis);

  const Mat4d xform(Vec4d(xAxis.x, xAxis.y, xAxis.z, 0.0),
      Vec4d(yAxis.x, yAxis.y, yAxis.z, 0.0),
      Vec4d(zAxis.x, zAxis.y, zAxis.z, 0.0),
      Vec4d(p.x, p.y, p.z, 1.0));

  const float radius = m_radius > 0.f ? m_radius : kMinimumRadius;

  // normalized = false makes color * intensity the radiance of the lens, and
  // a disk of radius r with radiance L has an on-axis intensity L * pi * r^2.
  const float radiance = m_intensity / (kPi * radius * radius);

  // Full cone angles in degrees, as MoonRay wants them.
  const float outer = toDegrees(m_openingAngle);
  const float inner = toDegrees(m_openingAngle - 2.f * m_falloffAngle);

  obj.set("node_xform", xform);
  obj.set("lens_radius", radius);
  obj.set("aspect_ratio", 1.f);
  obj.set("outer_cone_angle", std::min(outer, 179.9f));
  obj.set("inner_cone_angle", std::min(inner, 179.9f));
  obj.set("angle_falloff_type", 4); // ease in/out
  obj.set("black_level", 0.f);
  obj.set("normalized", false);
  obj.set("apply_scene_scale", false);
  obj.set("color", Rgb(m_color.x, m_color.y, m_color.z));
  obj.set("intensity", radiance);
  obj.set("exposure", 0.f);
  obj.set("visible_in_camera", 0);
}

} // namespace moonray_anari
