// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

#include <vector>

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// Base of all ANARI lights. An instance of *this* class is what an unknown
// light subtype produces: isValid() == false, so it never reaches MoonRay.
struct Light : public Object
{
  Light(MoonrayDeviceGlobalState *d);
  ~Light() override = default;

  static Light *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *d);

  void commitParameters() override;

  bool isValid() const override;

  // The RDL2 SceneClass this light maps to (nullptr for unknown subtypes).
  virtual const char *rdl2ClassName() const;

  // Write this light into `obj`, transformed by the enclosing ANARI
  // instance's transform. The caller holds an UpdateGuard on `obj` and runs on
  // the MoonRay backend thread.
  virtual void syncRdl2(
      scene_rdl2::rdl2::SceneObject &obj, const mat4 &instanceXfm) const;

  // `falloff.distance` and `falloff.color`: what the emission is multiplied
  // with, over the distance from the point of the light which emits to the
  // point which is lit. Linear between the distances, constant before the
  // first and after the last. Empty without one: the light then falls off
  // with the square of the distance only, as every light does.
  const std::vector<float> &falloffDistances() const;
  const std::vector<float3> &falloffColors() const;

 protected:
  float3 m_color{1.f, 1.f, 1.f};
  std::vector<float> m_falloffDistances;
  std::vector<float3> m_falloffColors;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Light *, ANARI_LIGHT);
