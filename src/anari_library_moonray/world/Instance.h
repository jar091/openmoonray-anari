// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

namespace moonray_anari {

// Which kinds of rays see an instance: one bit per MoonRay geometry
// visibility attribute (rdl2::Geometry's "visible_*"), in the order
// MoonrayScene writes them.
//
// ANARI has no such notion; the instance parameters `visibility.camera`,
// `.diffuse`, `.glossy`, `.transmission`, `.volumeScatter` and `.shadow` (all
// BOOL, default true) are a vendor addition with the meaning Cycles gives its
// object ray visibility, which is what the applications that set them have
// in mind:
//
//   camera          camera rays                       -> Camera
//   diffuse         rays after a diffuse reflection   -> DiffuseReflection
//   glossy          rays after a glossy or a mirror   -> GlossyReflection,
//                   reflection                           MirrorReflection
//   transmission    rays after any transmission       -> DiffuseTransmission,
//                                                        GlossyTransmission,
//                                                        MirrorTransmission
//   volumeScatter   rays scattered inside a volume    -> Phase
//   shadow          shadow (occlusion) rays           -> Shadow
//
// MoonRay splits by lobe type *and* by side where Cycles splits by lobe type
// for reflection and lumps all transmission together, so every ANARI flag is
// a union of MoonRay flags and nothing is approximated.
namespace visibility {
enum : uint32_t
{
  Camera = 1u << 0,
  Shadow = 1u << 1,
  DiffuseReflection = 1u << 2,
  DiffuseTransmission = 1u << 3,
  GlossyReflection = 1u << 4,
  GlossyTransmission = 1u << 5,
  MirrorReflection = 1u << 6,
  MirrorTransmission = 1u << 7,
  Phase = 1u << 8,
  All = (1u << 9) - 1u,
  None = 0u
};
} // namespace visibility

// This basically implements just the 'transform' instance subtype. Use the
// pattern other objects employ for having multiple subtypes if your device
// wants to implement other instance subtypes.
struct Instance : public Object
{
  Instance(MoonrayDeviceGlobalState *s);
  ~Instance() override = default;

  void commitParameters() override;

  uint32_t id() const;

  const mat4 &xfm() const;
  const mat3 &xfmInvRot() const;
  bool xfmIsIdentity() const;

  // The rays that see this instance, as a mask of `visibility` bits.
  uint32_t visibility() const;

  // What a shading network may read of the object next to its transform:
  // the parameters `color` (FLOAT32_VEC3 or VEC4), `objectRandom` (FLOAT32,
  // a vendor addition) and `id`. MoonrayScene hands them to the shaders as
  // primitive attributes of the instance (appendObjectInfo()).
  bool hasObjectColor() const;
  const float4 &objectColor() const;
  bool hasObjectRandom() const;
  float objectRandom() const;

  const Group *group() const;
  Group *group();

  bool isValid() const override;

 private:
  uint32_t m_id{~0u};
  mat4 m_xfm;
  mat3 m_xfmInvRot;
  uint32_t m_visibility{visibility::All};
  bool m_hasObjectColor{false};
  float4 m_objectColor{0.f, 0.f, 0.f, 1.f};
  bool m_hasObjectRandom{false};
  float m_objectRandom{0.f};
  helium::IntrusivePtr<Group> m_group;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Instance *, ANARI_INSTANCE);
