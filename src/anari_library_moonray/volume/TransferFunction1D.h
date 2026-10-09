// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Volume.h"
#include "array/Array1D.h"
#include "spatial_field/SpatialField.h"
// std
#include <string>
#include <vector>

namespace scene_rdl2 {
namespace rdl2 {
class SceneObject;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

// ANARI 'transferFunction1D' volume -> a MoonRay VdbGeometry plus a VdbVolume
// shader, fed by an OpenVDB file the device writes.
//
// MoonRay's volume model is exactly ANARI's: the renderer integrates an
// extinction and a scattering albedo along the ray, and `VdbVolume` computes
// them from the sampled grid value as
//
//     extinction = density * opacity_gain_mult
//     scattering = density * color_mult
//
// (VdbVolume.cc). So ANARI's opacity becomes the density grid - scaled by
// 1/unitDistance, since ANARI's opacity is per `unitDistance` of path - and
// ANARI's color becomes the albedo, either as the constant `color_mult` or,
// when it varies, as an OpenVdbMap reading a second grid.
//
// **The one approximation**, stated because it is real: ANARI interpolates the
// *field* and then applies the transfer function, while this device applies
// the transfer function per voxel and lets MoonRay interpolate the result.
// The two agree exactly at voxel centers, for a linear transfer function, and
// for `filter = "nearest"`; they differ elsewhere, by however much the
// transfer function curves between two neighbouring voxel values. MoonRay
// reads volumes from OpenVDB files and has no hook to classify after
// interpolation, so the alternative would be to not support volumes at all.
// To keep that difference small for the coarse fields where it shows most -
// a 4x4x4 field whose voxels are all 0 or 1, classified by a transfer
// function that is only opaque in between (pynari sample03.py) - a small
// field is resampled (trilinearly, as ANARI interpolates it) onto a finer
// grid before it is classified.
//
// ANARI's opacity is the `opacity` parameter times the alpha of `color`, so a
// float4 color array without an `opacity` array carries the opacity alone.
struct TransferFunction1D : public Volume
{
  TransferFunction1D(MoonrayDeviceGlobalState *s);
  ~TransferFunction1D() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  // The volume's geometry: a VdbGeometry pointing at the baked file.
  const char *rdl2ClassName() const;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const;

  // The volume shader the layer assigns to that geometry.
  const char *volumeShaderClassName() const;
  void syncVolumeShader(scene_rdl2::rdl2::SceneObject &obj) const;

  // A varying transfer-function color needs a per-voxel map bound to the
  // shader's `color_mult`; a constant one does not.
  bool needsColorMap() const;
  const char *colorMapClassName() const;
  void syncColorMap(scene_rdl2::rdl2::SceneObject &obj) const;

 private:
  // Bakes the field through the transfer function into a scratch .vdb.
  bool writeVolumeFile();
  // The transfer function at a normalized position in [0,1].
  float opacityAt(float t) const;
  float3 colorAt(float t) const;
  // How many baked voxels each field cell is split into, per axis.
  uint32_t refinement() const;

  helium::IntrusivePtr<SpatialField> m_field;
  helium::ChangeObserverPtr<Array1D> m_colorArray;
  helium::ChangeObserverPtr<Array1D> m_opacityArray;
  float2 m_valueRange{0.f, 1.f};
  float3 m_color{1.f, 1.f, 1.f};
  float m_opacity{1.f};
  float m_unitDistance{1.f};
  bool m_visible{true};

  // The transfer function, sampled: empty when the parameter was a constant.
  std::vector<float3> m_colors;
  std::vector<float> m_alphas; // the alpha of `color`, empty when it has none
  std::vector<float> m_opacities;
  float m_colorAlpha{1.f}; // the alpha of a constant `color`

  std::string m_volumePath;
  bool m_varyingColor{false};
  bool m_nearest{false};
  bool m_valid{false};
};

} // namespace moonray_anari
