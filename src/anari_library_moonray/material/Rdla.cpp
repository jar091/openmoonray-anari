// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "Rdla.h"
#include "sampler/Image2D.h"

// std
#include <algorithm>

namespace moonray_anari {

Rdla::Rdla(MoonrayDeviceGlobalState *s) : Material(s) {}

void Rdla::commitParameters()
{
  const std::string source = getParamString("source", "");
  const std::string materialName = getParamString("materialName", "");

  if (source.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'source' on the rdla material");
  }
  if (materialName.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'materialName' on the rdla material");
  }

  if (m_fragment.version == 0 || source != m_fragment.source
      || materialName != m_fragment.materialName) {
    m_fragment.source = source;
    m_fragment.materialName = materialName;
    ++m_fragment.version;
  }

  m_emitter = Emitter();
  m_emitter.color = getParam<float3>("emissionColor", float3(0.f, 0.f, 0.f));
  m_emitter.mapName = getParamString("emissionMap", "");
  m_emitter.sampledMaterialName = getParamString("sampledMaterialName", "");
  const int sides = emissionSides();
  const bool emits = m_emitter.color.x > 0.f || m_emitter.color.y > 0.f
      || m_emitter.color.z > 0.f;
  if (sides != 0 && m_emitter.sampledMaterialName.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'emissionSampling' on the rdla material needs 'sampledMaterialName', "
        "the root Material of the fragment that emits for camera and mirror "
        "rays only - the emission is not sampled");
  } else if (emits) {
    m_emitter.sides = sides;
  }

  // The images are whatever sampler parameters the application set: their
  // names are the application's choice, so they are found by type.
  std::vector<std::string> names;
  for (auto it = params_begin(); it != params_end(); ++it) {
    if (it->second.type() == ANARI_SAMPLER)
      names.push_back(it->first);
  }
  std::sort(names.begin(), names.end());

  m_imageParameters.clear();
  for (const auto &name : names) {
    auto *sampler = getParamObject<Sampler>(name);
    if (!sampler)
      continue;
    if (!dynamic_cast<Image2D *>(sampler)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' on the rdla material is not an image2D sampler - it cannot be "
          "used as an image of the material",
          name.c_str());
      continue;
    }
    m_imageParameters.push_back({name, sampler});
  }
}

bool Rdla::isValid() const
{
  return true;
}

const Material::Fragment *Rdla::fragment() const
{
  return &m_fragment;
}

Material::Emitter Rdla::emitter() const
{
  return m_emitter;
}

std::vector<Material::FragmentImage> Rdla::fragmentImages() const
{
  std::vector<FragmentImage> images;
  images.reserve(m_imageParameters.size());
  for (const auto &parameter : m_imageParameters) {
    // An image that cannot be used was already reported by its sampler.
    std::string error;
    auto *image = static_cast<Image2D *>(parameter.sampler.ptr);
    if (texture::TexturePtr texture = image->plainTexture(error))
      images.push_back({parameter.name, std::move(texture)});
  }
  return images;
}

} // namespace moonray_anari
