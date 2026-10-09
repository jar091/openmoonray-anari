// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SpatialField.h"
#include "array/Array3D.h"

namespace moonray_anari {

// ANARI 'structuredRegular' spatial field: a scalar value per grid vertex,
// with the grid described by an origin and a spacing. It carries no RDL2
// object of its own - the volume that uses it bakes it into an OpenVDB file,
// which is the only way MoonRay reads volume data.
struct StructuredRegular : public SpatialField
{
  StructuredRegular(MoonrayDeviceGlobalState *s);
  ~StructuredRegular() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  uint3 dimensions() const override;
  float3 origin() const override;
  float3 spacing() const override;
  bool nearestFilter() const override;
  bool readValues(std::vector<float> &out) const override;

 private:
  helium::ChangeObserverPtr<Array3D> m_data;
  float3 m_origin{0.f, 0.f, 0.f};
  float3 m_spacing{1.f, 1.f, 1.f};
  std::string m_filter{"linear"};
  bool m_valid{false};
};

} // namespace moonray_anari
