// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransferFunction1D.h"
#include "geometry/Geometry.h" // anariColorsToRgba
#include "texture/ScratchFile.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// anari
#include <anari/frontend/type_utility.h>
// OpenVDB
#include <openvdb/openvdb.h>
// std
#include <algorithm>
#include <cmath>
#include <mutex>

namespace moonray_anari {

namespace {

void ensureOpenVdbInitialized()
{
  static std::once_flag once;
  std::call_once(once, [] { openvdb::initialize(); });
}

// Samples an ANARI transfer-function array at a normalized position, linearly
// between its entries - which is how ANARI defines a 1D transfer function.
template <typename T>
T sampleArray(const std::vector<T> &values, float t)
{
  if (values.empty())
    return T{};
  if (values.size() == 1)
    return values.front();

  const float x = std::clamp(t, 0.f, 1.f) * float(values.size() - 1);
  const size_t i = std::min(size_t(x), values.size() - 2);
  const float f = x - float(i);
  return values[i] * (1.f - f) + values[i + 1] * f;
}

} // namespace

TransferFunction1D::TransferFunction1D(MoonrayDeviceGlobalState *s)
    : Volume(s), m_colorArray(this), m_opacityArray(this)
{}

TransferFunction1D::~TransferFunction1D()
{
  scratch::removeFile(m_volumePath);
}

void TransferFunction1D::commitParameters()
{
  Volume::commitParameters();

  m_field = getParamObject<SpatialField>("value");
  m_colorArray = getParamObject<Array1D>("color");
  m_opacityArray = getParamObject<Array1D>("opacity");
  m_visible = getParam<bool>("visible", true);
  m_unitDistance = getParam<float>("unitDistance", 1.f);

  float box[2] = {0.f, 1.f};
  if (getParam("valueRange", ANARI_FLOAT32_BOX1, box))
    m_valueRange = float2(box[0], box[1]);
  else
    m_valueRange = float2(0.f, 1.f);

  if (!m_colorArray) {
    float4 c(1.f, 1.f, 1.f, 1.f);
    float3 c3(1.f, 1.f, 1.f);
    if (getParam("color", ANARI_FLOAT32_VEC4, &c)) {
      m_color = float3(c.x, c.y, c.z);
      m_colorAlpha = c.w;
    } else if (getParam("color", ANARI_FLOAT32_VEC3, &c3)) {
      m_color = c3;
      m_colorAlpha = 1.f;
    } else {
      m_color = float3(1.f, 1.f, 1.f);
      m_colorAlpha = 1.f;
    }
  }
  if (!m_opacityArray)
    m_opacity = getParam<float>("opacity", 1.f);
}

void TransferFunction1D::finalize()
{
  m_valid = false;
  m_colors.clear();
  m_alphas.clear();
  m_opacities.clear();
  m_varyingColor = false;

  if (!m_field || !m_field->isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the transferFunction1D volume needs a valid 'value' spatial field");
    return;
  }

  if (!(m_unitDistance > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'unitDistance' on the transferFunction1D volume must be positive, "
        "got %g",
        m_unitDistance);
    return;
  }

  if (m_valueRange.y <= m_valueRange.x) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'valueRange' on the transferFunction1D volume is empty (%g, %g)",
        m_valueRange.x,
        m_valueRange.y);
    return;
  }

  if (m_colorArray) {
    std::vector<float> rgba;
    if (!anariColorsToRgba(m_colorArray->elementType(),
            m_colorArray->data(),
            m_colorArray->size(),
            rgba)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'color' on the transferFunction1D volume has an element type this "
          "device cannot read (%s)",
          anari::toString(m_colorArray->elementType()));
      return;
    }
    m_colors.reserve(rgba.size() / 4);
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
      m_colors.emplace_back(rgba[i], rgba[i + 1], rgba[i + 2]);
    // A color with alpha scales the opacity by it (and is the whole opacity
    // when there is no `opacity` array); a color without one leaves the
    // opacity alone.
    if (anari::componentsOf(m_colorArray->elementType()) == 4) {
      m_alphas.reserve(rgba.size() / 4);
      for (size_t i = 3; i < rgba.size(); i += 4)
        m_alphas.push_back(rgba[i]);
    }
    // A one-entry array is a constant, and a constant needs no per-voxel map.
    m_varyingColor = m_colors.size() > 1;
  }

  if (m_opacityArray) {
    if (m_opacityArray->elementType() != ANARI_FLOAT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'opacity' on the transferFunction1D volume must be an "
          "ANARI_FLOAT32 array");
      return;
    }
    const auto *o = m_opacityArray->beginAs<float>();
    m_opacities.assign(o, o + m_opacityArray->size());
  }

  m_nearest = m_field->nearestFilter();

  if (!m_visible) {
    // Nothing to render, and nothing to write: an invisible volume simply
    // stays out of the scene.
    return;
  }

  m_valid = writeVolumeFile();
}

float TransferFunction1D::opacityAt(float t) const
{
  const float opacity =
      m_opacities.empty() ? m_opacity : sampleArray(m_opacities, t);
  const float alpha = m_colorArray
      ? (m_alphas.empty() ? 1.f : sampleArray(m_alphas, t))
      : m_colorAlpha;
  return opacity * alpha;
}

float3 TransferFunction1D::colorAt(float t) const
{
  return m_colors.empty() ? m_color : sampleArray(m_colors, t);
}

uint32_t TransferFunction1D::refinement() const
{
  // A transfer function that is the same everywhere classifies every point
  // alike, so interpolating before or after classification is the same thing;
  // and with nearest filtering the field is piecewise constant anyway.
  if (m_nearest || (m_colors.size() <= 1 && m_alphas.size() <= 1
          && m_opacities.size() <= 1))
    return 1;

  // Otherwise small fields are resampled to about 64 voxels along their
  // longest axis. That gives the transfer function room to show (at 32 a
  // narrow transfer function band still shows as stripes, pynari
  // sample03.py), and not much more: MoonRay marches a volume in steps of
  // its voxel size (divided by volume_quality), so every further refinement
  // makes every ray through it more expensive.
  const uint3 size = m_field->dimensions();
  const uint32_t cells =
      std::max({size.x, size.y, size.z}) - 1; // dimensions are >= 2
  constexpr uint32_t target = 64;
  return std::clamp(target / std::max(cells, 1u), 1u, 64u);
}

bool TransferFunction1D::writeVolumeFile()
{
  std::vector<float> values;
  if (!m_field->readValues(values)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the transferFunction1D volume could not read its spatial field");
    return false;
  }

  const uint3 fieldSize = m_field->dimensions();
  const float3 origin = m_field->origin();
  // The baked grid: every field cell split into `r` voxels per axis, so the
  // field's own samples stay voxel centers of the baked grid.
  const uint32_t r = refinement();
  const uint3 size((fieldSize.x - 1) * r + 1,
      (fieldSize.y - 1) * r + 1,
      (fieldSize.z - 1) * r + 1);
  const double voxelSize = double(m_field->spacing().x) / double(r);
  const float lo = m_valueRange.x;
  const float invRange = 1.f / (m_valueRange.y - m_valueRange.x);

  ensureOpenVdbInitialized();

  auto density = openvdb::FloatGrid::create(0.f);
  density->setName("density");
  density->setGridClass(openvdb::GRID_FOG_VOLUME);
  auto xform = openvdb::math::Transform::createLinearTransform(voxelSize);
  xform->postTranslate(openvdb::Vec3d(origin.x, origin.y, origin.z));
  density->setTransform(xform);

  openvdb::Vec3SGrid::Ptr color;
  if (m_varyingColor) {
    color = openvdb::Vec3SGrid::create(openvdb::Vec3s(0.f, 0.f, 0.f));
    color->setName("color");
    color->setTransform(xform);
  }

  auto densityAccessor = density->getAccessor();
  std::unique_ptr<openvdb::Vec3SGrid::Accessor> colorAccessor;
  if (color) {
    colorAccessor =
        std::make_unique<openvdb::Vec3SGrid::Accessor>(color->getAccessor());
  }

  // The field value at baked voxel (i, j, k): trilinear between the field's
  // samples, as ANARI interpolates a structuredRegular field.
  const float invR = 1.f / float(r);
  auto fieldValue = [&](uint32_t i, uint32_t j, uint32_t k) {
    if (r == 1)
      return values[(size_t(k) * fieldSize.y + j) * fieldSize.x + i];
    const uint32_t i0 = std::min(i / r, fieldSize.x - 2);
    const uint32_t j0 = std::min(j / r, fieldSize.y - 2);
    const uint32_t k0 = std::min(k / r, fieldSize.z - 2);
    const float fx = float(i) * invR - float(i0);
    const float fy = float(j) * invR - float(j0);
    const float fz = float(k) * invR - float(k0);
    auto at = [&](uint32_t x, uint32_t y, uint32_t z) {
      return values[(size_t(z) * fieldSize.y + y) * fieldSize.x + x];
    };
    const float c00 = at(i0, j0, k0) * (1.f - fx) + at(i0 + 1, j0, k0) * fx;
    const float c10 =
        at(i0, j0 + 1, k0) * (1.f - fx) + at(i0 + 1, j0 + 1, k0) * fx;
    const float c01 =
        at(i0, j0, k0 + 1) * (1.f - fx) + at(i0 + 1, j0, k0 + 1) * fx;
    const float c11 = at(i0, j0 + 1, k0 + 1) * (1.f - fx)
        + at(i0 + 1, j0 + 1, k0 + 1) * fx;
    const float c0 = c00 * (1.f - fy) + c10 * fy;
    const float c1 = c01 * (1.f - fy) + c11 * fy;
    return c0 * (1.f - fz) + c1 * fz;
  };

  for (uint32_t k = 0; k < size.z; ++k) {
    for (uint32_t j = 0; j < size.y; ++j) {
      for (uint32_t i = 0; i < size.x; ++i) {
        const float t = (fieldValue(i, j, k) - lo) * invRange;
        // ANARI's opacity is per `unitDistance` of path, and MoonRay's
        // extinction is per unit length, so the grid holds opacity /
        // unitDistance.
        const float sigma = opacityAt(t) / m_unitDistance;
        const openvdb::Coord coord{int(i), int(j), int(k)};
        // A zero extinction is left inactive: it keeps the grid sparse and
        // the volume's bounds tight, which is what MoonRay renders.
        if (sigma > 0.f)
          densityAccessor.setValue(coord, sigma);
        if (colorAccessor) {
          const float3 c = colorAt(t);
          colorAccessor->setValue(coord, openvdb::Vec3s(c.x, c.y, c.z));
        }
      }
    }
  }

  if (density->activeVoxelCount() == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the transferFunction1D volume is completely transparent - every "
        "voxel's opacity is zero");
    return false;
  }

  std::string error;
  const std::string path = scratch::newFilePath("volume", this, "vdb", error);
  if (path.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the transferFunction1D volume could not prepare its file: %s",
        error.c_str());
    return false;
  }

  try {
    openvdb::GridPtrVec grids;
    grids.push_back(density);
    if (color)
      grids.push_back(color);
    openvdb::io::File file(path);
    file.setCompression(openvdb::io::COMPRESS_NONE);
    file.write(grids);
    file.close();
  } catch (const std::exception &e) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the transferFunction1D volume could not write '%s': %s",
        path.c_str(),
        e.what());
    scratch::removeFile(path);
    return false;
  }

  scratch::removeFile(m_volumePath);
  m_volumePath = path;
  return true;
}

bool TransferFunction1D::isValid() const
{
  return m_valid;
}

const char *TransferFunction1D::rdl2ClassName() const
{
  return "VdbGeometry";
}

void TransferFunction1D::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  obj.set("model", m_volumePath);
  obj.set("density_grid", std::string("density"));
  // 0 == nearest neighbor, 1 == linear. ANARI's `filter` is on the field, and
  // this is where it lands.
  obj.set("interpolation", m_nearest ? 0 : 1);
}

const char *TransferFunction1D::volumeShaderClassName() const
{
  return "VdbVolume";
}

void TransferFunction1D::syncVolumeShader(
    scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::Rgb;

  // The grid already holds the extinction, so the gain is the identity.
  obj.set("opacity_gain_mult", Rgb(1.f, 1.f, 1.f));
  // The scattering albedo. When it varies per voxel, a map is bound to this
  // attribute instead - and, as everywhere in MoonRay, a bound map multiplies
  // the attribute, so it is white in that case.
  const float3 c = m_varyingColor ? float3(1.f, 1.f, 1.f) : colorAt(0.5f);
  obj.set("color_mult", Rgb(c.x, c.y, c.z));
  obj.set("incandescence_gain_mult", Rgb(0.f, 0.f, 0.f));
  // ANARI's transferFunction1D has no phase-function parameter, so the volume
  // is isotropic.
  obj.set("anisotropy", 0.f);
}

bool TransferFunction1D::needsColorMap() const
{
  return m_varyingColor;
}

const char *TransferFunction1D::colorMapClassName() const
{
  return "OpenVdbMap";
}

void TransferFunction1D::syncColorMap(scene_rdl2::rdl2::SceneObject &obj) const
{
  // 0 == "from texture": the same file the geometry reads, sampled by
  // position, which is what the grid's transform is for.
  obj.set("vdb_source", 0);
  obj.set("texture", m_volumePath);
  obj.set("grid_name", std::string("color"));
  obj.set("texture_coordinates", 0); // position
  obj.set("interpolation", m_nearest ? 0 : 1);
}

} // namespace moonray_anari
