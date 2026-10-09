// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace moonray_anari {

// ANARI 'spot' light -> MoonRay SpotLight.
//
// MoonRay's SpotLight is a small emitting disk (the "lens") whose light is
// limited to a cone, with a falloff between an inner and an outer cone. A
// disk of radius r emitting radiance L has an on-axis intensity of
// I = L * pi * r^2, which is what the conversion uses; a requested radius of
// zero is rendered with a small one, as for the point light.
//
// The ANARI falloff region (falloffAngle, measured inwards from the edge of
// the cone) maps to the inner cone; MoonRay's ease in/out falloff is a
// smoothstep, like the falloff of most renderers.
struct Spot : public Light
{
  Spot(MoonrayDeviceGlobalState *s);
  ~Spot() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj,
      const mat4 &instanceXfm) const override;

 private:
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float m_intensity{1.f};
  float m_openingAngle{3.14159265358979323846f};
  float m_falloffAngle{0.1f};
  float m_radius{0.f};
};

} // namespace moonray_anari
