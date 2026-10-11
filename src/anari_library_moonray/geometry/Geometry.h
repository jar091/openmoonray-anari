// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
// std
#include <initializer_list>
#include <string>
#include <vector>

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// Base of all ANARI geometries. An instance of *this* class is what an unknown
// geometry subtype produces: isValid() == false, so it never reaches MoonRay.
struct Geometry : public Object
{
  Geometry(MoonrayDeviceGlobalState *s);
  ~Geometry() override = default;

  static Geometry *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *s);

  bool isValid() const override;

  // The RDL2 SceneClass this geometry maps to (nullptr for unknown subtypes).
  virtual const char *rdl2ClassName() const;

  // Write this geometry into `obj`. The caller holds an UpdateGuard on `obj`
  // and runs on the MoonRay backend thread.
  virtual void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const;

  // The area of the surface in its own space, 0 for a geometry that cannot
  // carry a mesh light (everything but the triangle geometry).
  virtual float surfaceArea() const;

  // The vertex positions moved by `distance` along the normals (the given
  // ones, or else those of the faces around a vertex), three floats each in
  // the order syncRdl2() writes them; a negative distance moves them to the
  // back. False for a geometry that cannot carry a mesh light.
  virtual bool displacedPositions(
      float distance, std::vector<float> &positions) const;
  // Half the diagonal of the bounds of the vertex positions, 0 without any.
  virtual float boundingRadius() const;
  // The number of triangles, 0 for a geometry that cannot carry a mesh light.
  virtual size_t triangleCount() const;

  // Per-vertex data that is not part of the geometry's own RDL2 attributes -
  // ANARI's `vertex.color` and `vertex.attribute*` - reaches MoonRay as
  // *primitive attributes*, which are `UserData` scene objects listed in the
  // geometry's `primitive_attributes`. Only the scene translation can create
  // those objects, so a geometry describes what it wants and MoonrayScene
  // builds them (the same split the material's sampler bindings use).
  struct PrimitiveAttribute
  {
    // The name the shading side looks the attribute up by. "Cd" is MoonRay's
    // own name for a vertex color.
    std::string name;
    enum class Type
    {
      Color, // three floats, written as color_values_0
      Float,
      Vec2f,
      Vec3f
    };
    Type type{Type::Color};
    // Component-major values: 3 per entry for Color/Vec3f, 2 for Vec2f, 1 for
    // Float.
    std::vector<float> values;
    // UserData's rate enum; 4 is "vertex".
    int rate{4};
  };
  virtual std::vector<PrimitiveAttribute> primitiveAttributes() const;

  // The name `vertex.attribute0` is exported under as a primitive attribute
  // (its first two components, vertex rate), for samplers that read their
  // coordinates through an AttributeMap rather than from the surface's st -
  // the image1D sampler, which has to work on every geometry, not only on
  // meshes.
  static constexpr const char *attribute0Name = "anari_attribute0";

  // The names of all four attribute slots as primitive attributes, for
  // shading networks that name the geometry's data themselves (the `rdla`
  // material): `anari_attribute0` .. `anari_attribute3`, each a vec3f holding
  // the slot's first three components. MoonRay keys a primitive attribute on
  // its name *and* type, so the vec2f `anari_attribute0` above and the vec3f
  // one are two attributes that happen to share a name.
  static const char *attributeName(int slot);

 protected:
  // Warns about every listed parameter the application set but this device
  // does not implement. Advertising a geometry subtype means advertising its
  // whole parameter list, so the ones that do nothing have to say so.
  void reportUnsupportedParameters(
      std::initializer_list<const char *> names) const;
};

// ANARI colors come in fixed-point and float flavours of one to four
// components; MoonRay's primitive attributes are RGB. Returns false (leaving
// `out` untouched) for an element type that is not a color.
bool anariColorsToRgb(const Array1D &array, std::vector<float> &out);

// The same with the alpha kept: four values per entry, alpha 1 for element
// types that carry none. Works on any array memory (`n` entries of `type`),
// so 2D images - the renderer's background - can use it too.
bool anariColorsToRgba(
    ANARIDataType type, const void *data, size_t n, std::vector<float> &out);

// The same for ANARI's float attribute arrays: `components` values per entry,
// zero-filled when the array is narrower and truncated when it is wider, which
// is what ANARI's attribute expansion rules say.
bool anariFloatsToComponents(
    const Array1D &array, int components, std::vector<float> &out);

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Geometry *, ANARI_GEOMETRY);
