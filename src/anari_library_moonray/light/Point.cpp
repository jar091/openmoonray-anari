// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Point.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <cmath>

namespace moonray_anari {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// What a "radius 0" point light becomes. Small enough that the illumination
// at any sane distance matches a delta light (the error is of order
// (r/d)^2), large enough that MoonRay's sampling stays well conditioned.
constexpr float kMinimumRadius = 1e-3f;

} // namespace

Point::Point(MoonrayDeviceGlobalState *s) : Light(s) {}

void Point::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_intensity = getParam<float>("intensity", 1.f);
  m_radius = getParam<float>("radius", 0.f);

  if (hasParam("power")) {
    // Power is the total flux: I = P / (4 pi) for a light that emits equally
    // in every direction.
    m_intensity = getParam<float>("power", 0.f) / (4.f * kPi);
  } else if (hasParam("radiance")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'radiance' is not supported on the point light yet - 'intensity' or "
        "'power' is used instead");
  }

  if (m_radius < 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'radius' must not be negative on the point light - using 0");
    m_radius = 0.f;
  }
}

bool Point::isValid() const
{
  return m_intensity > 0.f;
}

const char *Point::rdl2ClassName() const
{
  return "SphereLight";
}

void Point::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, const mat4 &instanceXfm) const
{
  using scene_rdl2::rdl2::Mat4d;
  using scene_rdl2::rdl2::Rgb;
  using scene_rdl2::rdl2::Vec4d;

  const float4 p = linalg::mul(
      instanceXfm, float4(m_position.x, m_position.y, m_position.z, 1.f));

  const Mat4d xform(Vec4d(1.0, 0.0, 0.0, 0.0),
      Vec4d(0.0, 1.0, 0.0, 0.0),
      Vec4d(0.0, 0.0, 1.0, 0.0),
      Vec4d(p.x, p.y, p.z, 1.0));

  const float radius = m_radius > 0.f ? m_radius : kMinimumRadius;

  // normalized = false makes color * intensity the light's radiance
  // (LightUtil.h computeLightRadiance), and a sphere of radius r with
  // radiance L has intensity L * pi * r^2 in every direction.
  const float radiance = m_intensity / (kPi * radius * radius);

  obj.set("node_xform", xform);
  obj.set("radius", radius);
  obj.set("normalized", false);
  obj.set("apply_scene_scale", false);
  obj.set("color", Rgb(m_color.x, m_color.y, m_color.z));
  obj.set("intensity", radiance);
  obj.set("exposure", 0.f);
  obj.set("visible_in_camera", 0);
}

} // namespace moonray_anari
