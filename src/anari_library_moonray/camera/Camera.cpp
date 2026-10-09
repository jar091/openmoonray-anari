// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
#include "Orthographic.h"
#include "Perspective.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Camera::Camera(MoonrayDeviceGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera *Camera::createInstance(
    std::string_view type, MoonrayDeviceGlobalState *s)
{
  if (type == "perspective")
    return new Perspective(s);
  if (type == "orthographic")
    return new Orthographic(s);
  // unknown subtype: valid handle, isValid() == false
  auto *camera = new Camera(s);
  reportUnknownSubtype(camera, "camera", type);
  return camera;
}

void Camera::commitParameters()
{
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_direction =
      linalg::normalize(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  m_up = linalg::normalize(getParam<float3>("up", float3(0.f, 1.f, 0.f)));
}

bool Camera::isValid() const
{
  return false;
}

const char *Camera::rdl2ClassName() const
{
  return nullptr;
}

void Camera::syncRdl2(scene_rdl2::rdl2::SceneObject &, float) const
{
  // Unknown subtype: nothing to write.
}

float2 Camera::imagePlaneHalfExtent(float) const
{
  return float2(0.f, 0.f);
}

void Camera::syncTransform(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::Mat4d;
  using scene_rdl2::rdl2::Vec4d;

  const float3 right = linalg::normalize(linalg::cross(m_direction, m_up));
  const float3 up = linalg::normalize(linalg::cross(right, m_direction));
  const float3 back = -m_direction;

  // RDL2 uses row vectors (v' = v * M), so the basis vectors are the rows.
  const Mat4d xform(Vec4d(right.x, right.y, right.z, 0.0),
      Vec4d(up.x, up.y, up.z, 0.0),
      Vec4d(back.x, back.y, back.z, 0.0),
      Vec4d(m_position.x, m_position.y, m_position.z, 1.0));

  obj.set("node_xform", xform);
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Camera *);
