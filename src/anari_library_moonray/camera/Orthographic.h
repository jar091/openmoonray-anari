// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
// std
#include <optional>

namespace moonray_anari {

// ANARI 'orthographic' camera -> MoonRay OrthographicCamera.
//
// For an orthographic camera MoonRay's aperture window scales world units
// directly (OrthographicCamera::computeProjectionMatrix), so film_width_
// aperture *is* the visible width and the mapping is one subtraction away
// from ANARI's `height`/`aspect` (docs/ANARI_MOONRAY_MAPPING.md).
struct Orthographic : public Camera
{
  Orthographic(MoonrayDeviceGlobalState *s);
  ~Orthographic() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(
      scene_rdl2::rdl2::SceneObject &obj, float imageAspect) const override;

 private:
  float m_height{1.f};
  std::optional<float> m_aspect;
  float m_near{1e-3f};
  float m_far{1e6f};
};

} // namespace moonray_anari
