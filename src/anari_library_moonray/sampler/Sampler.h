// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// Base of all ANARI samplers. An instance of *this* class is what an unknown
// sampler subtype produces: isValid() == false, so it never reaches MoonRay.
struct Sampler : public Object
{
  Sampler(MoonrayDeviceGlobalState *s);
  ~Sampler() override = default;

  static Sampler *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *s);

  bool isValid() const override;

  // The RDL2 Map SceneClass this sampler maps to (nullptr when unknown).
  virtual const char *rdl2ClassName() const;

  // Write this sampler into `obj`. The caller holds an UpdateGuard on `obj`
  // and runs on the MoonRay backend thread.
  virtual void syncRdl2(scene_rdl2::rdl2::SceneObject &obj);

  // The geometry primitive attribute (a vec2f) the map reads its texture
  // coordinates from, through an AttributeMap bound to the map's
  // `input_texture_coordinates`; nullptr when the map samples the surface's
  // own st instead. Only the scene translation can create that AttributeMap,
  // so the sampler names the attribute and MoonrayScene builds the binding.
  virtual const char *inputPrimitiveAttribute() const;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Sampler *, ANARI_SAMPLER);
