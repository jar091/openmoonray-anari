// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
#include "Directional.h"
#include "HDRI.h"
#include "Point.h"
#include "Quad.h"
#include "Spot.h"

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
