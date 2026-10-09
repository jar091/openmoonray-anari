// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace moonray_anari {

// ANARI 'point' light -> MoonRay SphereLight.
//
// MoonRay has no delta point light: every light it can sample is an area
// light. A sphere of radius r emitting radiance L uniformly has intensity
// I = L * pi * r^2 in every direction, so the conversion is exact for a
// finite radius, and a requested radius of zero is rendered with a small one
// (docs/ANARI_MOONRAY_MAPPING.md says how small and what that costs).
struct Point : public Light
{
  Point(MoonrayDeviceGlobalState *s);
  ~Point() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj,
      const mat4 &instanceXfm) const override;

 private:
  float3 m_position{0.f, 0.f, 0.f};
  float m_intensity{1.f};
  float m_radius{0.f};
};

} // namespace moonray_anari
