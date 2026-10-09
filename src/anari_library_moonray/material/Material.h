// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"
// std
#include <vector>

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// Base of all ANARI materials. An instance of *this* class is what an unknown
// material subtype produces: isValid() == false, so it never reaches MoonRay.
struct Material : public Object
{
  Material(MoonrayDeviceGlobalState *s);
  ~Material() override = default;

  static Material *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *s);

  bool isValid() const override;

  // The RDL2 SceneClass this material maps to (nullptr for unknown subtypes).
  virtual const char *rdl2ClassName() const;

  // Write this material into `obj`. The caller holds an UpdateGuard on `obj`
  // and runs on the MoonRay backend thread.
  virtual void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const;

  // "This parameter comes from a texture" is an RDL2 *binding* from one of
  // the material's attributes to a Map object. Only the scene translation can
  // create that Map, so a material reports which of its attributes wants
  // which sampler and MoonrayScene binds them (docs/ANARI_MOONRAY_MAPPING.md).
  struct SamplerBinding
  {
    const char *rdl2Attribute{nullptr};
    Sampler *sampler{nullptr};
  };
  virtual std::vector<SamplerBinding> samplerBindings() const;

  // The same arrangement for a parameter whose value comes from the
  // *geometry*: ANARI lets a material name a per-vertex attribute instead of
  // carrying a value, and MoonRay reads primitive attributes through an
  // `AttributeMap` bound to the material's own attribute.
  struct AttributeBinding
  {
    const char *rdl2Attribute{nullptr};
    // The primitive attribute's name, as the geometry writes it ("Cd" for
    // ANARI's `vertex.color`).
    std::string primitiveAttribute;
    // AttributeMap's `primitive_attribute_type` enum: 0 float, 1 vec2f,
    // 2 vec3f, 3 rgb.
    int primitiveAttributeType{3};
  };
  virtual std::vector<AttributeBinding> attributeBindings() const;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Material *, ANARI_MATERIAL);
