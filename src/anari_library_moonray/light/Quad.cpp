// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Quad.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <cmath>
#include <string>

namespace moonray_anari {

Quad::Quad(MoonrayDeviceGlobalState *s) : Light(s) {}

void Quad::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_edge1 = getParam<float3>("edge1", float3(1.f, 0.f, 0.f));
  m_edge2 = getParam<float3>("edge2", float3(0.f, 1.f, 0.f));

  const float area =
      linalg::length(linalg::cross(m_edge1, m_edge2));
  m_valid = area > 0.f;
  if (!m_valid) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'edge1' and 'edge2' of the quad light are parallel, so it has no "
        "area");
    return;
  }

  // ANARI offers three ways to say how bright the quad is; they are the same
  // quantity divided by the emitting area or the hemisphere it emits into.
  if (hasParam("radiance")) {
    m_radiance = getParam<float>("radiance", 1.f);
  } else if (hasParam("power")) {
    // Power is the total flux of a Lambertian emitter: P = L * area * pi.
    m_radiance = getParam<float>("power", 0.f) / (area * 3.14159265358979f);
  } else {
    // Intensity is per steradian, along the normal: I = L * area.
    m_radiance = getParam<float>("intensity", 1.f) / area;
  }

  const std::string side = getParamString("side", "front");
  if (side == "front") {
    m_sidedness = 0;
  } else if (side == "back") {
    m_sidedness = 1;
  } else if (side == "both") {
    m_sidedness = 2;
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'side' \"%s\" on the quad light - using \"front\"",
        side.c_str());
    m_sidedness = 0;
  }

  if (hasParam("intensityDistribution")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'intensityDistribution' is not supported on the quad light");
  }
}

bool Quad::isValid() const
{
  return m_valid && m_radiance > 0.f;
}

const char *Quad::rdl2ClassName() const
{
  return "RectLight";
}

void Quad::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, const mat4 &instanceXfm) const
{
  using scene_rdl2::rdl2::Mat4d;
  using scene_rdl2::rdl2::Rgb;
  using scene_rdl2::rdl2::Vec4d;

  auto transformPoint = [&](const float3 &p) {
    const float4 v = linalg::mul(instanceXfm, float4(p.x, p.y, p.z, 1.f));
    return float3(v.x, v.y, v.z);
  };
  auto transformVector = [&](const float3 &v) {
    const float4 t = linalg::mul(instanceXfm, float4(v.x, v.y, v.z, 0.f));
    return float3(t.x, t.y, t.z);
  };

  const float3 corner = transformPoint(m_position);
  const float3 edge1 = transformVector(m_edge1);
  const float3 edge2 = transformVector(m_edge2);

  // ANARI's position is a *corner*; MoonRay's rect is centered on its node.
  const float3 center = corner + 0.5f * (edge1 + edge2);
  const float width = linalg::length(edge1);
  const float height = linalg::length(edge2);

  const float3 xAxis = edge1 / width;
  const float3 yAxis = edge2 / height;
  // The emitting normal is edge1 x edge2; RectLight emits along the negative
  // z axis of node_xform, so that is where the normal goes.
  const float3 normal = linalg::normalize(linalg::cross(edge1, edge2));
  const float3 zAxis = -normal;

  const Mat4d xform(Vec4d(xAxis.x, xAxis.y, xAxis.z, 0.0),
      Vec4d(yAxis.x, yAxis.y, yAxis.z, 0.0),
      Vec4d(zAxis.x, zAxis.y, zAxis.z, 0.0),
      Vec4d(center.x, center.y, center.z, 1.0));

  obj.set("node_xform", xform);
  obj.set("width", width);
  obj.set("height", height);
  // normalized = false makes color * intensity the radiance directly.
  obj.set("normalized", false);
  obj.set("apply_scene_scale", false);
  obj.set("color", Rgb(m_color.x, m_color.y, m_color.z));
  obj.set("intensity", m_radiance);
  obj.set("exposure", 0.f);
  obj.set("sidedness", m_sidedness);
  obj.set("visible_in_camera", 0);
}

} // namespace moonray_anari
