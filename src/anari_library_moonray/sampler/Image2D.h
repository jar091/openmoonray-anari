// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array2D.h"
// std
#include <string>

namespace moonray_anari {

// ANARI 'image2D' sampler -> MoonRay ImageMap.
//
// MoonRay samples textures through OpenImageIO's texture system, which reads
// *files*: `ImageMap`'s `texture` parameter is a path, and there is no
// in-memory entry point. ANARI hands the device pixels in an ANARIArray2D, so
// the device writes them to a scratch OpenEXR file and points the map at it.
// That is the honest cost of the mapping, not a shortcut - see
// docs/ANARI_MOONRAY_MAPPING.md. The file is rewritten under a new name
// whenever the image changes, which also keeps OpenImageIO's texture cache
// from serving stale pixels, and is removed when the sampler goes away.
struct Image2D : public Sampler
{
  Image2D(MoonrayDeviceGlobalState *s);
  ~Image2D() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) override;

 private:
  // Writes the current image to a new scratch file and removes the previous
  // one. Returns false (and reports why) if the file could not be written.
  bool writeTextureFile();

  helium::ChangeObserverPtr<Array2D> m_image;
  std::string m_inAttribute{"attribute0"};
  std::string m_filter{"linear"};
  std::string m_wrapMode1{"clampToEdge"};
  std::string m_wrapMode2{"clampToEdge"};

  std::string m_texturePath;
  // ImageMap's texture coordinate transform: st' = scale * R(-angle) * st +
  // offset, derived from 'inTransform'.
  float m_uvScale[2]{1.f, 1.f};
  float m_uvOffset[2]{0.f, 0.f};
  float m_uvRotationDegrees{0.f};
  bool m_valid{false};
};

} // namespace moonray_anari
