// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// std
#include <string>

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// What a material does with the value a sampler returns. It decides how the
// sampler's RDL2 map has to deliver it, because MoonRay evaluates a bound map
// differently per attribute type (shading/EvalAttribute.h):
//
//  * Color  - an Rgb attribute (diffuseColor, emissiveColor): the map's rgb,
//             multiplied by the attribute's own value;
//  * Scalar - a Float attribute (roughness, metallic, opacity, ...): the
//             *average* of the map's rgb, multiplied by the attribute's own
//             value. ANARI means the sampler's first component, so the map
//             has to be arranged for that average to be it;
//  * Normal - the tangent space normal, a Vec3f attribute: the map's rgb as
//             it is, nothing multiplied. ANARI's `normal` sampler returns
//             the *encoded* normal, a color in [0, 1], as it sits in a normal
//             map (the convention of the ANARI SDK's glTF loader and of the
//             other devices), so the map also has to decode it: n = 2 c - 1,
//             applied to the sampler's output, i.e. after its own output
//             transform.
enum class SamplerUsage
{
  Color,
  Scalar,
  Normal
};

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

  // Whether the sampler can feed a parameter used as `usage`; `why` says why
  // not. A sampler is one ANARI object but one RDL2 map per usage, so the
  // answer can differ between the parameters it is assigned to.
  virtual bool supports(SamplerUsage usage, std::string &why) const;

  // Write this sampler's map for `usage` into `obj`. The caller holds an
  // UpdateGuard on `obj` and runs on the MoonRay backend thread.
  virtual void syncRdl2(scene_rdl2::rdl2::SceneObject &obj, SamplerUsage usage);

  // What the material attribute the map is bound to has to hold. MoonRay
  // multiplies a bound map by the attribute's own value, so this is normally
  // one - and a sampler that scales its output without a map being able to
  // (the `transform` sampler) puts the scale here.
  virtual float3 attributeFactor(SamplerUsage usage) const;

  // The geometry primitive attribute the map reads its texture coordinates
  // from, through an AttributeMap bound to the map's
  // `input_texture_coordinates`; nullptr when the map samples the surface's
  // own st instead (or reads no coordinates at all). Only the scene
  // translation can create that AttributeMap, so the sampler names the
  // attribute and MoonrayScene builds the binding.
  virtual const char *inputPrimitiveAttribute() const;
  // AttributeMap's `primitive_attribute_type` of that attribute: 1 vec2f,
  // 2 vec3f.
  virtual int inputPrimitiveAttributeType() const;

 protected:
  // The MoonRay primitive attribute an ANARI attribute name ("attribute0" ..
  // "attribute3", "color") is exported as by the geometries, and its
  // AttributeMap type; false for a name that is none of them.
  static bool primitiveAttribute(
      const std::string &anariName, const char *&name, int &type);
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Sampler *, ANARI_SAMPLER);
