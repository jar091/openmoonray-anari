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

  float region[4] = {0.f, 0.f, 1.f, 1.f};
  if (hasParam("imageRegion")
      && !getParam("imageRegion", ANARI_FLOAT32_BOX2, region)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'imageRegion' must be an ANARI_FLOAT32_BOX2 - using the full image "
        "plane");
  }
  if (!(region[2] > region[0]) || !(region[3] > region[1])) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'imageRegion' (%g, %g)-(%g, %g) is empty or reversed - using the "
        "full image plane",
        region[0],
        region[1],
        region[2],
        region[3]);
    region[0] = region[1] = 0.f;
    region[2] = region[3] = 1.f;
  }
  for (int i = 0; i < 4; ++i)
    m_imageRegion[i] = region[i];
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

float4 Camera::imagePlaneWindow(float) const
{
  return float4(0.f, 0.f, 0.f, 0.f);
}

Camera::Film Camera::film(
    float planeWidth, float planeHeight, float imageAspect) const
{
  const float x0 = m_imageRegion[0], y0 = m_imageRegion[1];
  const float x1 = m_imageRegion[2], y1 = m_imageRegion[3];
  const float a = imageAspect > 0.f ? imageAspect : 1.f;

  // MoonRay's window spans, with half the film width hf and the offsets o,
  //
  //     x: (-hf + ox) .. (hf + ox)
  //     y: ((-hf / a) + oy) / par .. ((hf / a) + oy) / par
  //
  // and has to be the region: x from W (x0 - 1/2) to W (x1 - 1/2) and y from
  // H (y0 - 1/2) to H (y1 - 1/2) for a plane of W x H around the axis.
  Film f;
  f.width = planeWidth * (x1 - x0);
  f.offsetX = planeWidth * 0.5f * (x0 + x1 - 1.f);
  // The frame's aspect need not be the region's: the pixel aspect ratio is
  // what stretches the window's height to the region's.
  f.pixelAspectRatio = f.width / (a * planeHeight * (y1 - y0));
  f.offsetY = f.pixelAspectRatio * planeHeight * 0.5f * (y0 + y1 - 1.f);
  return f;
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
