// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "Image1D.h"
#include "Image2D.h"
#include "Transform.h"
#include "geometry/Geometry.h"

namespace moonray_anari {

Sampler::Sampler(MoonrayDeviceGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler *Sampler::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  if (subtype == "image2D")
    return new Image2D(s);
  if (subtype == "transform")
    return new Transform(s);
  // unknown subtype: isValid() == false
  auto *sampler = new Sampler(s);
  reportUnknownSubtype(sampler, "sampler", subtype);
  return sampler;
}

bool Sampler::isValid() const
{
  return false;
}

const char *Sampler::rdl2ClassName() const
{
  return nullptr;
}

bool Sampler::supports(SamplerUsage, std::string &) const
{
  return true;
}

void Sampler::syncRdl2(scene_rdl2::rdl2::SceneObject &, SamplerUsage)
{
  // Unknown subtype: nothing to write.
}

float3 Sampler::attributeFactor(SamplerUsage) const
{
  return float3(1.f, 1.f, 1.f);
}

const char *Sampler::inputPrimitiveAttribute() const
{
  return nullptr;
}

int Sampler::inputPrimitiveAttributeType() const
{
  return 1; // vec2f
}

bool Sampler::primitiveAttribute(
    const std::string &anariName, const char *&name, int &type)
{
  if (anariName == "color") {
    // MoonRay's own vertex color attribute, an rgb.
    name = "Cd";
    type = 3;
    return true;
  }
  static const char *const slots[4] = {
      "attribute0", "attribute1", "attribute2", "attribute3"};
  for (int slot = 0; slot < 4; ++slot) {
    if (anariName == slots[slot]) {
      name = Geometry::attributeName(slot);
      type = 2; // vec3f
      return true;
    }
  }
  return false;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Sampler *);
