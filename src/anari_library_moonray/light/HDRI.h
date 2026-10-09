// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
#include "array/Array2D.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'hdri' light -> MoonRay EnvLight with a lat-long texture.
//
// MoonRay's EnvLight reads its map from a *file* (pbr::ImageDistribution opens
// it with OpenImageIO), so the ANARI radiance array is written to a scratch
// texture exactly as the image2D sampler's is - see texture/ScratchTexture.h.
//
// The orientation is where the work is. MoonRay maps a direction to the map
// through a fixed lat-long parameterization in the light's own frame, and that
// frame is the node transform composed with a fixed rotation
// (EnvLight::sLocalOrientation). ANARI says only that `direction` is "the
// direction to which the center of the texture is mapped" and that `up` is the
// up direction; the derivation of the node transform that satisfies both, and
// the check that the resulting azimuth handedness matches the reference
// implementation the ANARI wording comes from, is in
// docs/ANARI_MOONRAY_MAPPING.md.
struct HDRI : public Light
{
  HDRI(MoonrayDeviceGlobalState *s);
  ~HDRI() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;
  // Whether the environment is the visible background.
  bool visible() const
  {
    return m_visible;
  }

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj,
      const mat4 &instanceXfm) const override;

 private:
  // Writes the current radiance image to a new scratch file and removes the
  // previous one. Returns false (and reports why) if it could not be written.
  bool writeTextureFile();

  helium::ChangeObserverPtr<Array2D> m_radiance;
  float3 m_up{0.f, 0.f, 1.f};
  float3 m_direction{1.f, 0.f, 0.f};
  float m_scale{1.f};
  // Whether the environment is seen by camera rays (the background).
  bool m_visible{true};

  std::string m_texturePath;
  bool m_valid{false};
};

} // namespace moonray_anari
