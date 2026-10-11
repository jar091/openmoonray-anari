// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"
#include "texture/Texture.h"
// std
#include <cstdint>
#include <string>
#include <utility>
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
  // `usage` is what the attribute does with the map's value, which decides
  // how the sampler has to deliver it (SamplerUsage); one sampler bound as a
  // color and as a scalar is two maps.
  struct SamplerBinding
  {
    const char *rdl2Attribute{nullptr};
    Sampler *sampler{nullptr};
    SamplerUsage usage{SamplerUsage::Color};
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

  // A material that is not one RDL2 object the device fills in, but a piece
  // of RDL2 scene text that creates the material and its maps itself (the
  // `rdla` subtype). MoonrayScene loads it instead of calling syncRdl2().
  struct Fragment
  {
    // RDL2 ASCII (Lua).
    std::string source;
    // Name of the root rdl2::Material the text creates.
    std::string materialName;
    // Moves whenever one of the above changes, and only then; 0 means the
    // material has not been committed yet.
    uint64_t version{0};
  };
  // Null for every material that is a single object.
  virtual const Fragment *fragment() const;

  // The images of a fragment: a string attribute of one of its objects whose
  // value is `name` is given the texture's name instead. Asked for at every
  // scene sync rather than kept in the Fragment, because what matters is the
  // pixels behind the samplers at that moment - the texture of an image
  // whose pixels did not change is the same object as before
  // (texture::acquireTexture()), so comparing textures is comparing images.
  struct FragmentImage
  {
    std::string name;
    texture::TexturePtr texture;
  };
  virtual std::vector<FragmentImage> fragmentImages() const;

  // --- emission that lights sample ----------------------------------------
  // MoonRay only finds an emitting surface when a path happens to hit it,
  // which is noise (fireflies from a small lamp) and loses a bounce. The
  // `emissionSampling` parameter, a vendor addition every material subtype
  // has, asks the device to sample the emission with lights as well: "front",
  // "back" or "both" sides ("none" is the default). MoonrayScene then puts a
  // MeshLight per side on a copy of the geometry of every instance, and
  // shades the surface with a variant of the material that only emits for
  // camera and mirror rays - what the lights are not seen by - so that
  // nothing is counted twice.
  struct Emitter
  {
    // Bit 0: the front side is sampled, bit 1: the back side. 0: nothing is.
    int sides{0};
    // The radiance, which a map is multiplied with when there is one.
    float3 color{0.f, 0.f, 0.f};
    // A material that is one RDL2 object: the attribute of that object which
    // holds the emission (the map of its sampler binding is the light's).
    const char *rdl2Attribute{nullptr};
    // An `rdla` material: the Map of the fragment the radiance is multiplied
    // with (`emissionMap`, may be empty), and the root Material of the
    // fragment which emits for camera and mirror rays only
    // (`sampledMaterialName`).
    std::string mapName;
    std::string sampledMaterialName;
  };
  // sides == 0 for a material that does not emit or was not asked to.
  virtual Emitter emitter() const;

  // Whether the material can be absent at a point of its surface (a cutout).
  // MoonRay only asks a material for its presence along *shadow* rays when
  // the scene says so (`enable_presence_shadows`, which makes every shadow
  // ray more expensive), so MoonrayScene switches that on while a material
  // that needs it is in the scene - otherwise a leaf cut out of a card would
  // still cast the card's shadow.
  virtual bool usesPresence() const;

 protected:
  // The sampler of a sampler-valued parameter if it can feed `usage` as it
  // is now, null otherwise (no sampler, an invalid one, or one that cannot
  // do this - Sampler::supports()). Asked at sync time by samplerBindings()
  // and syncRdl2() alike, so the binding and the constant written into the
  // bound attribute always agree.
  static Sampler *usable(
      const helium::IntrusivePtr<Sampler> &sampler, SamplerUsage usage);

  // The `emissionSampling` parameter as Emitter::sides, with a warning for a
  // value that is none of the four.
  int emissionSides() const;

  // Warns when `parameter` holds a sampler that usable() rejects, so that
  // the constant rendered instead does not come as a surprise. For
  // finalize(): by then the samplers committed together with the material
  // are in their new state.
  void reportUnusable(const char *parameter,
      const helium::IntrusivePtr<Sampler> &sampler,
      SamplerUsage usage) const;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Material *, ANARI_MATERIAL);
