// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "StructuredRegular.h"

// std
#include <cstdint>

namespace moonray_anari {

namespace {

// ANARI's scalar field element types, normalized the way ANARI defines them:
// UFIXED to [0,1], FIXED16 to [-1,1], float straight through.
bool readScalar(ANARIDataType type, const void *data, size_t i, float &out)
{
  switch (type) {
  case ANARI_FLOAT32:
    out = static_cast<const float *>(data)[i];
    return true;
  case ANARI_FLOAT64:
    out = float(static_cast<const double *>(data)[i]);
    return true;
  case ANARI_UFIXED8:
    out = static_cast<const uint8_t *>(data)[i] / 255.f;
    return true;
  case ANARI_UFIXED16:
    out = static_cast<const uint16_t *>(data)[i] / 65535.f;
    return true;
  case ANARI_FIXED16:
    out = std::max(-1.f, static_cast<const int16_t *>(data)[i] / 32767.f);
    return true;
  default:
    return false;
  }
}

} // namespace

StructuredRegular::StructuredRegular(MoonrayDeviceGlobalState *s)
    : SpatialField(s), m_data(this)
{}

void StructuredRegular::commitParameters()
{
  m_data = getParamObject<Array3D>("data");
  m_origin = getParam<float3>("origin", float3(0.f, 0.f, 0.f));
  m_spacing = getParam<float3>("spacing", float3(1.f, 1.f, 1.f));
  m_filter = getParamString("filter", "linear");
}

void StructuredRegular::finalize()
{
  m_valid = false;

  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on the structuredRegular spatial "
        "field");
    return;
  }

  float probe = 0.f;
  if (!m_data->data() || !readScalar(m_data->elementType(), m_data->data(), 0, probe)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on the structuredRegular spatial field must be a scalar array "
        "(%s is not one this device can read)",
        anari::toString(m_data->elementType()));
    return;
  }

  const uint3 size = m_data->size();
  if (size.x < 2 || size.y < 2 || size.z < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the structuredRegular spatial field needs at least 2x2x2 voxels, got "
        "%ux%ux%u",
        size.x,
        size.y,
        size.z);
    return;
  }

  if (!(m_spacing.x > 0.f && m_spacing.y > 0.f && m_spacing.z > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'spacing' on the structuredRegular spatial field must be positive");
    return;
  }
  if (m_spacing.x != m_spacing.y || m_spacing.y != m_spacing.z) {
    // OpenVDB's linear transform can express non-uniform voxels, but MoonRay
    // reads the grid through its own transform handling, so this device only
    // claims what it has tested.
    reportMessage(ANARI_SEVERITY_WARNING,
        "a non-uniform 'spacing' (%g, %g, %g) is not supported; the x spacing "
        "is used for all three axes",
        m_spacing.x,
        m_spacing.y,
        m_spacing.z);
  }

  if (m_filter != "linear" && m_filter != "nearest") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'filter' \"%s\" on the structuredRegular spatial field - "
        "using linear",
        m_filter.c_str());
    m_filter = "linear";
  }

  m_valid = true;
}

bool StructuredRegular::isValid() const
{
  return m_valid;
}

uint3 StructuredRegular::dimensions() const
{
  return m_valid ? m_data->size() : uint3(0u, 0u, 0u);
}

float3 StructuredRegular::origin() const
{
  return m_origin;
}

float3 StructuredRegular::spacing() const
{
  return m_spacing;
}

bool StructuredRegular::nearestFilter() const
{
  return m_filter == "nearest";
}

bool StructuredRegular::readValues(std::vector<float> &out) const
{
  if (!m_valid)
    return false;

  const uint3 size = m_data->size();
  const size_t n = size_t(size.x) * size_t(size.y) * size_t(size.z);
  const ANARIDataType type = m_data->elementType();
  const void *data = m_data->data();

  out.resize(n);
  for (size_t i = 0; i < n; ++i) {
    if (!readScalar(type, data, i, out[i]))
      return false;
  }
  return true;
}

} // namespace moonray_anari
