// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// std
#include <vector>

namespace moonray_anari {

// Base of all ANARI spatial fields. An instance of *this* class is what an
// unknown subtype produces: isValid() == false, so it never reaches MoonRay.
//
// A spatial field has no RDL2 object of its own. MoonRay's volumes are read
// from OpenVDB files, so the field is data that the volume bakes into one -
// see volume/TransferFunction1D.h and docs/ANARI_MOONRAY_MAPPING.md.
struct SpatialField : public Object
{
  SpatialField(MoonrayDeviceGlobalState *d);
  virtual ~SpatialField() = default;
  static SpatialField *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *d);

  bool isValid() const override;

  // Voxel counts along each axis (all zero for an unknown subtype).
  virtual uint3 dimensions() const;
  // World-space position of voxel (0,0,0) and the step between voxels.
  virtual float3 origin() const;
  virtual float3 spacing() const;
  // True when the application asked for nearest-neighbour interpolation.
  virtual bool nearestFilter() const;

  // Every voxel as a float, x fastest. False when the field cannot be read.
  virtual bool readValues(std::vector<float> &out) const;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(
    moonray_anari::SpatialField *, ANARI_SPATIAL_FIELD);
