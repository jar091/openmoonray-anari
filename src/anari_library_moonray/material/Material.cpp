// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Matte.h"
#include "PhysicallyBased.h"
#include "Rdla.h"

namespace moonray_anari {

Material::Material(MoonrayDeviceGlobalState *s) : Object(ANARI_MATERIAL, s) {}

Material *Material::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  if (subtype == "physicallyBased")
    return new PhysicallyBased(s);
  if (subtype == "rdla")
    return new Rdla(s);
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

const Material::Fragment *Material::fragment() const
{
  return nullptr;
}

Material::Emitter Material::emitter() const
{
  return {};
}

int Material::emissionSides() const
{
  const std::string sampling = getParamString("emissionSampling", "none");
  if (sampling == "none")
    return 0;
  if (sampling == "front")
    return 1;
  if (sampling == "back")
    return 2;
  if (sampling == "both")
    return 3;
  reportMessage(ANARI_SEVERITY_WARNING,
      "unknown 'emissionSampling' \"%s\" - using \"none\" (the others are "
      "\"front\", \"back\" and \"both\")",
      sampling.c_str());
  return 0;
}

bool Material::usesPresence() const
{
  return false;
}

Sampler *Material::usable(
    const helium::IntrusivePtr<Sampler> &sampler, SamplerUsage usage)
{
  std::string why;
  if (!sampler || !sampler->isValid() || !sampler->supports(usage, why))
    return nullptr;
  return sampler.ptr;
}

void Material::reportUnusable(const char *parameter,
    const helium::IntrusivePtr<Sampler> &sampler,
    SamplerUsage usage) const
{
  if (!sampler)
    return;
  std::string why;
  if (!sampler->isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the sampler of '%s' is not valid (see its own warnings) - the "
        "parameter's default is used",
        parameter);
  } else if (!sampler->supports(usage, why)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the sampler of '%s' cannot be used: %s - the parameter's default is "
        "used",
        parameter,
        why.c_str());
  }
}

std::vector<Material::FragmentImage> Material::fragmentImages() const
{
  return {};
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Material *);
