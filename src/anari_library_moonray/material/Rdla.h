// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"
// std
#include <string>
#include <vector>

namespace moonray_anari {

// Material subtype 'rdla': a MoonRay shading network handed over as RDL2
// ASCII (Lua) text instead of being assembled from ANARI parameters.
//
//  * 'source' (STRING) is an rdla fragment that creates one Material and the
//    maps it uses, for example
//
//        ImageMap("m7/albedo") {
//            ["texture"] = "image.0",
//        }
//        DwaBaseMaterial("m7/surface") {
//            ["albedo"] = bind(ImageMap("m7/albedo"), Rgb(1, 1, 1)),
//        }
//
//  * 'materialName' (STRING) names the root Material object in the fragment;
//  * every parameter of type SAMPLER holding an image2D sampler is an image
//    of the fragment: a string attribute in the fragment whose value equals
//    the *name* of such a parameter ("image.0" above) is replaced by the
//    texture of that sampler's pixels. The sampler's own filter, wrap mode
//    and transforms are not used; the fragment has its own.
//
// The material itself only gathers these. Loading the text into the device's
// SceneContext, finding the root, retiring what an earlier version of the
// fragment created and falling back to a visible error material is
// MoonrayScene's job (syncFragment()), because all of it has to happen on the
// MoonRay backend thread.
struct Rdla : public Material
{
  Rdla(MoonrayDeviceGlobalState *s);
  ~Rdla() override = default;

  void commitParameters() override;

  // Always valid: a fragment that cannot be loaded renders with the error
  // material rather than making its surfaces disappear.
  bool isValid() const override;

  const Fragment *fragment() const override;
  std::vector<FragmentImage> fragmentImages() const override;
  // 'emissionSampling' together with what the device cannot read out of a
  // fragment: 'emissionColor' (FLOAT32_VEC3, the radiance), 'emissionMap'
  // (STRING, a Map of the fragment the radiance is multiplied with, optional)
  // and 'sampledMaterialName' (STRING, the root Material of the fragment
  // that emits for camera and mirror rays only).
  Emitter emitter() const override;

 private:
  struct ImageParameter
  {
    std::string name;
    helium::IntrusivePtr<Sampler> sampler;
  };

  std::vector<ImageParameter> m_imageParameters;
  Emitter m_emitter;
  // `version` only moves when the text or the material name differs from the
  // previous commit, which is what keeps an identical re-commit from
  // reloading anything.
  Fragment m_fragment;
};

} // namespace moonray_anari
