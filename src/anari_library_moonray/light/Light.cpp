// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
#include "Directional.h"
#include "HDRI.h"
#include "Point.h"
#include "Quad.h"
#include "Spot.h"

#include "array/Array1D.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Light::Light(MoonrayDeviceGlobalState *s) : Object(ANARI_LIGHT, s) {}

Light *Light::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "directional")
    return new Directional(s);
  if (subtype == "point")
    return new Point(s);
  if (subtype == "quad")
    return new Quad(s);
  if (subtype == "spot")
    return new Spot(s);
  if (subtype == "hdri")
    return new HDRI(s);
  // unknown subtype: isValid() == false
  auto *light = new Light(s);
  reportUnknownSubtype(light, "light", subtype);
  return light;
}

void Light::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));

  m_falloffDistances.clear();
  m_falloffColors.clear();
  const Array1D *distances = getParamObject<Array1D>("falloff.distance");
  const Array1D *colors = getParamObject<Array1D>("falloff.color");
  if (!distances && !colors)
    return;
  if (!distances || !colors || distances->elementType() != ANARI_FLOAT32
      || colors->elementType() != ANARI_FLOAT32_VEC3
      || distances->size() != colors->size() || distances->size() < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'falloff.distance' (FLOAT32) and 'falloff.color' (FLOAT32_VEC3) of a "
        "light have to be arrays of the same size, of two elements or more - "
        "the falloff is ignored");
    return;
  }
  const float *d = distances->beginAs<float>();
  const float3 *c = colors->beginAs<float3>();
  for (size_t i = 0; i < distances->size(); ++i) {
    if (!(d[i] >= 0.f) || (i > 0 && !(d[i] > d[i - 1]))) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'falloff.distance' of a light has to increase, from 0 or more - "
          "the falloff is ignored");
      m_falloffDistances.clear();
      m_falloffColors.clear();
      return;
    }
    m_falloffDistances.push_back(d[i]);
    m_falloffColors.push_back(c[i]);
  }
}

const std::vector<float> &Light::falloffDistances() const
{
  return m_falloffDistances;
}

const std::vector<float3> &Light::falloffColors() const
{
  return m_falloffColors;
}

bool Light::isValid() const
{
  return false;
}

const char *Light::rdl2ClassName() const
{
  return nullptr;
}

void Light::syncRdl2(scene_rdl2::rdl2::SceneObject &, const mat4 &) const
{
  // Unknown subtype: nothing to write.
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Light *);
