// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace moonray_anari {

// ANARI 'directional' light -> MoonRay DistantLight.
//
// The radiometric conversion is derived, not guessed: MoonRay's normalized
// DistantLight is defined so that radiance R produces outgoing radiance R off
// a white Lambertian surface facing the light, i.e. R = E/pi for irradiance E
// (docs/ANARI_MOONRAY_MAPPING.md).
struct Directional : public Light
{
  Directional(MoonrayDeviceGlobalState *s);
  ~Directional() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj,
      const mat4 &instanceXfm) const override;

 private:
  float3 m_direction{0.f, 0.f, -1.f};
  float m_irradiance{1.f};
  float m_angularDiameter{0.f};
};

} // namespace moonray_anari
