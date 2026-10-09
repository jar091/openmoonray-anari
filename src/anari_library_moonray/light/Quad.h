// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace moonray_anari {

// ANARI 'quad' light -> MoonRay RectLight.
//
// ANARI describes the quad by a corner and two edges; MoonRay by a width, a
// height and a transform, emitting along the negative z axis of node_xform
// (RectLight::update() folds in a 180 degree rotation about x, exactly like
// DistantLight). The mapping is therefore a change of parameterization plus
// the radiometric conversion in docs/ANARI_MOONRAY_MAPPING.md.
struct Quad : public Light
{
  Quad(MoonrayDeviceGlobalState *s);
  ~Quad() override = default;

  void commitParameters() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj,
      const mat4 &instanceXfm) const override;

 private:
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_edge1{1.f, 0.f, 0.f};
  float3 m_edge2{0.f, 1.f, 0.f};
  float m_radiance{1.f};
  // MoonRay's RectLight sidedness: 0 regular (front), 1 reverse (back),
  // 2 two-sided - the same three cases as ANARI's `side`.
  int m_sidedness{0};
  bool m_valid{true};
};

} // namespace moonray_anari
