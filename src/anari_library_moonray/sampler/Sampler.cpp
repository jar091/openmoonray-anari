// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "Image1D.h"
#include "Image2D.h"

namespace moonray_anari {

Sampler::Sampler(MoonrayDeviceGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler *Sampler::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  if (subtype == "image2D")
    return new Image2D(s);
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

void Sampler::syncRdl2(scene_rdl2::rdl2::SceneObject &)
{
  // Unknown subtype: nothing to write.
}

const char *Sampler::inputPrimitiveAttribute() const
{
  return nullptr;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Sampler *);
