// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sphere.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Sphere::Sphere(MoonrayDeviceGlobalState *s)
    : Geometry(s),
      m_vertexPosition(this),
      m_vertexRadius(this),
      m_vertexColor(this),
      m_vertexAttribute0(this),
      m_primitiveColor(this),
      m_index(this)
{}

void Sphere::commitParameters()
{
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_primitiveColor = getParamObject<Array1D>("primitive.color");
  m_index = getParamObject<Array1D>("primitive.index");
  m_radius = getParam<float>("radius", 1.f);

  reportUnsupportedParameters({"vertex.attribute1",
      "vertex.attribute2",
      "vertex.attribute3",
      "primitive.attribute0",
      "primitive.attribute1",
      "primitive.attribute2",
      "primitive.attribute3",
      "color",
      "attribute0",
      "attribute1",
      "attribute2",
      "attribute3"});
}

void Sphere::finalize()
{
  m_valid = false;
  m_points.clear();

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on sphere geometry");
    return;
  }
  if (m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.position' on sphere geometry must be ANARI_FLOAT32_VEC3");
    return;
  }

  const size_t numVertices = m_vertexPosition->size();

  // As on the triangle geometry: an optional attribute this device cannot use
  // is dropped with a warning instead of making the whole geometry disappear.
  // 'vertex.radius' has the constant 'radius' to fall back on.
  if (m_vertexRadius) {
    if (m_vertexRadius->elementType() != ANARI_FLOAT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.radius' on sphere geometry must be ANARI_FLOAT32 - "
          "ignoring it and using the constant 'radius'");
      m_vertexRadius = nullptr;
    } else if (m_vertexRadius->size() != numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.radius' has %zu entries but the geometry has %zu vertices "
          "- ignoring it and using the constant 'radius'",
          m_vertexRadius->size(),
          numVertices);
      m_vertexRadius = nullptr;
    }
  }

  if (!m_vertexRadius && !(m_radius > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'radius' on sphere geometry must be positive, got %f",
        m_radius);
    return;
  }

  if (m_vertexColor) {
    std::vector<float> probe;
    if (!anariColorsToRgb(*m_vertexColor, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.color' on sphere geometry has an element type this device "
          "cannot read (%s) - ignoring it",
          anari::toString(m_vertexColor->elementType()));
      m_vertexColor = nullptr;
    } else if (m_vertexColor->size() != numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.color' has %zu entries but the geometry has %zu vertices "
          "- ignoring it",
          m_vertexColor->size(),
          numVertices);
      m_vertexColor = nullptr;
    }
  }

  if (m_vertexAttribute0) {
    std::vector<float> probe;
    if (!anariFloatsToComponents(*m_vertexAttribute0, 2, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.attribute0' on sphere geometry must be a float, float2, "
          "float3 or float4 array - ignoring it");
      m_vertexAttribute0 = nullptr;
    } else if (m_vertexAttribute0->size() != numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.attribute0' has %zu entries but the geometry has %zu "
          "vertices - ignoring it",
          m_vertexAttribute0->size(),
          numVertices);
      m_vertexAttribute0 = nullptr;
    }
  }

  if (m_primitiveColor) {
    std::vector<float> probe;
    if (!anariColorsToRgb(*m_primitiveColor, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.color' on sphere geometry has an element type this "
          "device cannot read (%s) - ignoring it",
          anari::toString(m_primitiveColor->elementType()));
      m_primitiveColor = nullptr;
    }
  }

  if (m_index) {
    if (m_index->elementType() != ANARI_UINT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' on sphere geometry must be ANARI_UINT32");
      return;
    }
    const auto *idx = m_index->beginAs<uint32_t>();
    m_points.reserve(m_index->size());
    for (size_t i = 0; i < m_index->size(); ++i) {
      if (idx[i] >= numVertices) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "'primitive.index'[%zu] on sphere geometry is out of range "
            "(%zu vertices)",
            i,
            numVertices);
        m_points.clear();
        return;
      }
      m_points.push_back(idx[i]);
    }
  } else {
    m_points.resize(numVertices);
    for (size_t i = 0; i < numVertices; ++i)
      m_points[i] = uint32_t(i);
  }

  if (m_primitiveColor && m_primitiveColor->size() != m_points.size()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.color' has %zu entries but the geometry has %zu spheres "
        "- ignoring it",
        m_primitiveColor->size(),
        m_points.size());
    m_primitiveColor = nullptr;
  }

  m_valid = !m_points.empty();
  if (!m_valid) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "the sphere geometry has no spheres");
  }
}

bool Sphere::isValid() const
{
  return m_valid;
}

const char *Sphere::rdl2ClassName() const
{
  return "RdlPointGeometry";
}

void Sphere::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::FloatVector;
  using scene_rdl2::rdl2::Vec3fVector;

  const auto *positions = m_vertexPosition->beginAs<float3>();

  Vec3fVector vertices;
  vertices.reserve(m_points.size());
  for (uint32_t i : m_points)
    vertices.emplace_back(positions[i].x, positions[i].y, positions[i].z);

  FloatVector radii;
  if (m_vertexRadius) {
    const auto *r = m_vertexRadius->beginAs<float>();
    radii.reserve(m_points.size());
    for (uint32_t i : m_points)
      radii.push_back(r[i]);
  } else {
    // A single value is read as the constant rate (RdlPointGeometry's
    // pickRate), which is exactly what ANARI's global `radius` is.
    radii.push_back(m_radius);
  }

  obj.set("vertex_list_0", vertices);
  obj.set("radius_list", radii);
}

std::vector<Geometry::PrimitiveAttribute> Sphere::primitiveAttributes() const
{
  std::vector<PrimitiveAttribute> attributes;

  // One color per sphere either way: a sphere is both a vertex and a
  // primitive, and the per-vertex color wins when both are given.
  if (m_vertexColor) {
    std::vector<float> rgb;
    if (anariColorsToRgb(*m_vertexColor, rgb)) {
      PrimitiveAttribute cd;
      cd.name = "Cd";
      cd.type = PrimitiveAttribute::Type::Color;
      cd.rate = 4; // vertex: one color per sphere
      cd.values.reserve(3 * m_points.size());
      for (uint32_t i : m_points) {
        cd.values.push_back(rgb[3 * size_t(i) + 0]);
        cd.values.push_back(rgb[3 * size_t(i) + 1]);
        cd.values.push_back(rgb[3 * size_t(i) + 2]);
      }
      attributes.push_back(std::move(cd));
    }
  } else if (m_primitiveColor) {
    PrimitiveAttribute cd;
    cd.name = "Cd";
    cd.type = PrimitiveAttribute::Type::Color;
    cd.rate = 4; // vertex: sphere i is primitive i
    if (anariColorsToRgb(*m_primitiveColor, cd.values))
      attributes.push_back(std::move(cd));
  }

  if (m_vertexAttribute0) {
    std::vector<float> uv;
    if (anariFloatsToComponents(*m_vertexAttribute0, 2, uv)) {
      PrimitiveAttribute a0;
      a0.name = attribute0Name;
      a0.type = PrimitiveAttribute::Type::Vec2f;
      a0.rate = 4; // vertex: one value per sphere
      a0.values.reserve(2 * m_points.size());
      for (uint32_t i : m_points) {
        a0.values.push_back(uv[2 * size_t(i) + 0]);
        a0.values.push_back(uv[2 * size_t(i) + 1]);
      }
      attributes.push_back(std::move(a0));
    }
  }

  return attributes;
}

} // namespace moonray_anari
