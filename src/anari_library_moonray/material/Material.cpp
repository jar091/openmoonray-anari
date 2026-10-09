// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Matte.h"
#include "PhysicallyBased.h"

namespace moonray_anari {

Material::Material(MoonrayDeviceGlobalState *s) : Object(ANARI_MATERIAL, s) {}

Material *Material::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  if (subtype == "physicallyBased")
    return new PhysicallyBased(s);
  // unknown subtype: isValid() == false
  auto *material = new Material(s);
  reportUnknownSubtype(material, "material", subtype);
  return material;
}

bool Material::isValid() const
{
  return false;
}

const char *Material::rdl2ClassName() const
{
  return nullptr;
}

void Material::syncRdl2(scene_rdl2::rdl2::SceneObject &) const
{
  // Unknown subtype: nothing to write.
}

std::vector<Material::SamplerBinding> Material::samplerBindings() const
{
  return {};
}

std::vector<Material::AttributeBinding> Material::attributeBindings() const
{
  return {};
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Material *);
