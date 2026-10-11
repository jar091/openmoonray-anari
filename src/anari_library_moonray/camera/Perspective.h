// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
// std
#include <optional>

namespace moonray_anari {

// ANARI 'perspective' camera -> MoonRay PerspectiveCamera.
//
// MoonRay expresses the frustum as a focal length against a film-back width;
// docs/ANARI_MOONRAY_MAPPING.md derives the conversion from fovy/aspect.
struct Perspective : public Camera
{
  Perspective(MoonrayDeviceGlobalState *s);
  ~Perspective() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(
      scene_rdl2::rdl2::SceneObject &obj, float imageAspect) const override;
  float4 imagePlaneWindow(float imageAspect) const override;

 private:
  float m_fovy{1.0471976f}; // ANARI default: pi/3
  std::optional<float> m_aspect;
  float m_near{1e-3f};
  float m_far{1e6f};
  // KHR_CAMERA_DEPTH_OF_FIELD: a thin lens of this radius, focused at
  // focusDistance (both in world units); a zero radius is a pinhole.
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};
};

} // namespace moonray_anari
