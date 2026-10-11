// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "OutputTransform.h"
#include "Sampler.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'transform' sampler: a geometry attribute (`inAttribute`) run through
// `outTransform` / `outOffset` - a vertex color times a constant, say.
//
// MoonRay reads a primitive attribute through an `AttributeMap`, which has no
// arithmetic of its own, so the only transform that can be expressed without
// further maps is the one MoonRay applies anyway: a bound map is multiplied
// by the value of the attribute it is bound to. That makes a *color* with a
// per-channel scale exact (attributeFactor()), and nothing else - an offset,
// mixed channels and scalar parameters are refused (supports()) rather than
// approximated.
struct Transform : public Sampler
{
  Transform(MoonrayDeviceGlobalState *s);
  ~Transform() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  bool supports(SamplerUsage usage, std::string &why) const override;
  void syncRdl2(
      scene_rdl2::rdl2::SceneObject &obj, SamplerUsage usage) override;
  float3 attributeFactor(SamplerUsage usage) const override;

 private:
  std::string m_inAttribute{"attribute0"};
  OutputTransform m_output;
  // The MoonRay primitive attribute and its AttributeMap type.
  const char *m_attribute{nullptr};
  int m_attributeType{2};
  bool m_valid{false};
};

} // namespace moonray_anari
