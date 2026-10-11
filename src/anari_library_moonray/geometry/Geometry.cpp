// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
#include "Curve.h"
#include "Cylinder.h"
#include "Sphere.h"
#include "Triangle.h"
// std
#include <cmath>

namespace moonray_anari {

namespace {

float srgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

// How many components an element type carries, and how to read one of them as
// a normalized float. Returns 0 for types this device does not accept.
int componentCount(ANARIDataType type)
{
  switch (type) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED16:
  case ANARI_UFIXED32:
  case ANARI_FLOAT32:
    return 1;
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED32_VEC2:
  case ANARI_FLOAT32_VEC2:
    return 2;
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED32_VEC3:
  case ANARI_FLOAT32_VEC3:
    return 3;
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_RGBA_SRGB:
  case ANARI_UFIXED16_VEC4:
  case ANARI_UFIXED32_VEC4:
  case ANARI_FLOAT32_VEC4:
    return 4;
  default:
    return 0;
  }
}

bool isSrgb(ANARIDataType type)
{
  return type == ANARI_UFIXED8_R_SRGB || type == ANARI_UFIXED8_RA_SRGB
      || type == ANARI_UFIXED8_RGB_SRGB || type == ANARI_UFIXED8_RGBA_SRGB;
}

// Element size in bytes of one component.
int componentSize(ANARIDataType type)
{
  switch (type) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    return 1;
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4:
    return 2;
  default:
    return 4;
  }
}

bool isFixedPoint(ANARIDataType type)
{
  switch (type) {
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
    return false;
  default:
    return true;
  }
}

// Reads component `c` of entry `i`, normalized to [0,1] for fixed-point types.
float readComponent(
    const void *data, ANARIDataType type, int components, size_t i, int c)
{
  const size_t index = i * size_t(components) + size_t(c);
  if (!isFixedPoint(type))
    return static_cast<const float *>(data)[index];

  switch (componentSize(type)) {
  case 1:
    return static_cast<const uint8_t *>(data)[index] / 255.f;
  case 2:
    return static_cast<const uint16_t *>(data)[index] / 65535.f;
  default:
    return float(static_cast<const uint32_t *>(data)[index] / 4294967295.0);
  }
}

} // namespace

Geometry::Geometry(MoonrayDeviceGlobalState *s) : Object(ANARI_GEOMETRY, s) {}

Geometry *Geometry::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "triangle")
    return new Triangle(s);
  if (subtype == "sphere")
    return new Sphere(s);
  if (subtype == "curve")
    return new Curve(s);
  if (subtype == "cylinder")
    return new Cylinder(s, /*cone=*/false);
  if (subtype == "cone")
    return new Cylinder(s, /*cone=*/true);
  // unknown subtype: isValid() == false
  auto *geometry = new Geometry(s);
  reportUnknownSubtype(geometry, "geometry", subtype);
  return geometry;
}

bool Geometry::isValid() const
{
  return false;
}

const char *Geometry::rdl2ClassName() const
{
  return nullptr;
}

void Geometry::syncRdl2(scene_rdl2::rdl2::SceneObject &) const
{
  // Unknown subtype: nothing to write.
}

float Geometry::surfaceArea() const
{
  return 0.f;
}

bool Geometry::displacedPositions(float, std::vector<float> &) const
{
  return false;
}

float Geometry::boundingRadius() const
{
  return 0.f;
}

size_t Geometry::triangleCount() const
{
  return 0;
}

std::vector<Geometry::PrimitiveAttribute> Geometry::primitiveAttributes() const
{
  return {};
}

const char *Geometry::attributeName(int slot)
{
  static const char *const names[4] = {"anari_attribute0",
      "anari_attribute1",
      "anari_attribute2",
      "anari_attribute3"};
  return slot >= 0 && slot < 4 ? names[slot] : nullptr;
}

void Geometry::reportUnsupportedParameters(
    std::initializer_list<const char *> names) const
{
  for (const char *name : names) {
    if (hasParam(name)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' is set but not implemented by this device's geometry - it has "
          "no effect (SUPPORTED_FEATURES.md lists what is)",
          name);
    }
  }
}

bool anariColorsToRgb(const Array1D &array, std::vector<float> &out)
{
  const ANARIDataType type = array.elementType();
  const int components = componentCount(type);
  if (components == 0)
    return false;

  const void *data = array.data();
  if (!data)
    return false;

  const size_t n = array.size();
  const bool srgb = isSrgb(type);

  out.assign(3 * n, 0.f);
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) {
      // A one-component color is grey, a two-component one is (r, g, 0) - the
      // same expansion ANARI applies to a narrow attribute, except that the
      // alpha ANARI would put in .w has nowhere to go in an RGB attribute.
      const int src = components == 1 ? 0 : c;
      float v = src < components
          ? readComponent(data, type, components, i, src)
          : 0.f;
      if (srgb)
        v = srgbToLinear(v);
      out[3 * i + size_t(c)] = v;
    }
  }
  return true;
}

bool anariColorsToRgba(
    ANARIDataType type, const void *data, size_t n, std::vector<float> &out)
{
  const int components = componentCount(type);
  if (components == 0 || !data)
    return false;

  const bool srgb = isSrgb(type);
  out.assign(4 * n, 0.f);
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) {
      // ANARI's expansion: one component is grey (for colors), missing
      // components are zero.
      const int src = components == 1 ? 0 : c;
      float v = src < components
          ? readComponent(data, type, components, i, src)
          : 0.f;
      if (srgb)
        v = srgbToLinear(v);
      out[4 * i + size_t(c)] = v;
    }
    // Alpha is never sRGB encoded; a type without it is opaque.
    out[4 * i + 3] =
        components == 4 ? readComponent(data, type, components, i, 3) : 1.f;
  }
  return true;
}

bool anariFloatsToComponents(
    const Array1D &array, int components, std::vector<float> &out)
{
  const ANARIDataType type = array.elementType();
  const int have = componentCount(type);
  if (have == 0 || isFixedPoint(type))
    return false;

  const void *data = array.data();
  if (!data)
    return false;

  const size_t n = array.size();
  out.assign(size_t(components) * n, 0.f);
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < components; ++c) {
      out[size_t(components) * i + size_t(c)] =
          c < have ? readComponent(data, type, have, i, c) : 0.f;
    }
  }
  return true;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Geometry *);
