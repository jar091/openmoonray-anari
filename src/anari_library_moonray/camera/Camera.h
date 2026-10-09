// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// Base of all ANARI cameras. An instance of *this* class is what an unknown
// camera subtype produces: it reports isValid() == false, so the scene
// translation skips it and no rendering subtype is falsely claimed.
struct Camera : public Object
{
  Camera(MoonrayDeviceGlobalState *s);
  ~Camera() override = default;

  static Camera *createInstance(
      std::string_view type, MoonrayDeviceGlobalState *state);

  void commitParameters() override;

  bool isValid() const override;

  // The RDL2 SceneClass this camera maps to (nullptr for an unknown subtype).
  virtual const char *rdl2ClassName() const;

  // Write this camera into `obj`, an instance of rdl2ClassName(). The caller
  // holds an UpdateGuard on `obj` and runs on the MoonRay backend thread.
  // `imageAspect` is the frame's width/height.
  virtual void syncRdl2(
      scene_rdl2::rdl2::SceneObject &obj, float imageAspect) const;

  // Half-extent of the image plane one unit along the view axis, in camera
  // space. It is what turns MoonRay's camera-space depth into ANARI's
  // distance-along-the-ray: (0,0) means the rays are parallel to the axis and
  // the two are the same (docs/adr/0007).
  virtual float2 imagePlaneHalfExtent(float imageAspect) const;

 protected:
  // node_xform from position/direction/up. MoonRay cameras look down local
  // -Z, so the camera-to-world basis is [right, up, -direction, position].
  void syncTransform(scene_rdl2::rdl2::SceneObject &obj) const;

  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float3 m_up{0.f, 1.f, 0.f};
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Camera *, ANARI_CAMERA);
