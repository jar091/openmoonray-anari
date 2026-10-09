// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
#include "StructuredRegular.h"

namespace moonray_anari {

SpatialField::SpatialField(MoonrayDeviceGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{}

SpatialField *SpatialField::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegular(s);
  // unknown subtype: isValid() == false
  auto *field = new SpatialField(s);
  reportUnknownSubtype(field, "spatial field", subtype);
  return field;
}

bool SpatialField::isValid() const
{
  return false;
}

uint3 SpatialField::dimensions() const
{
  return uint3(0u, 0u, 0u);
}

float3 SpatialField::origin() const
{
  return float3(0.f, 0.f, 0.f);
}

float3 SpatialField::spacing() const
{
  return float3(1.f, 1.f, 1.f);
}

bool SpatialField::nearestFilter() const
{
  return false;
}

bool SpatialField::readValues(std::vector<float> &) const
{
  return false;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::SpatialField *);
