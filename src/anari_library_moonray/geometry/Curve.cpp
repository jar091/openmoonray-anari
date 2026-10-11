// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Curve.h"

// std
#include <cmath>

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Curve::Curve(MoonrayDeviceGlobalState *s)
    : Geometry(s),
      m_vertexPosition(this),
      m_vertexRadius(this),
      m_vertexColor(this),
      m_vertexAttribute0(this),
      m_vertexAttribute1(this),
      m_vertexAttribute2(this),
      m_vertexAttribute3(this),
      m_index(this)
{}

void Curve::commitParameters()
{
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<Array1D>("vertex.attribute3");
  m_index = getParamObject<Array1D>("primitive.index");
  m_radius = getParam<float>("radius", 1.f);

  reportUnsupportedParameters({"primitive.color",
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

void Curve::finalize()
{
  m_valid = false;
  m_vertices.clear();
  m_curveVertexCounts.clear();

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on curve geometry");
    return;
  }
  if (m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.position' on curve geometry must be ANARI_FLOAT32_VEC3");
    return;
  }

  const size_t numVertices = m_vertexPosition->size();
  if (numVertices < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "curve geometry needs at least two vertices, got %zu",
        numVertices);
    return;
  }

  // As on the triangle geometry: an optional attribute this device cannot use
  // is dropped with a warning instead of making the whole geometry disappear.
  // 'vertex.radius' has the constant 'radius' to fall back on.
  if (m_vertexRadius) {
    if (m_vertexRadius->elementType() != ANARI_FLOAT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.radius' on curve geometry must be ANARI_FLOAT32 - ignoring "
          "it and using the constant 'radius'");
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
        "'radius' on curve geometry must be positive, got %f",
        m_radius);
    return;
  }

  if (m_vertexColor) {
    std::vector<float> probe;
    if (!anariColorsToRgb(*m_vertexColor, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.color' on curve geometry has an element type this device "
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

  // The attribute slots: one value per vertex, of floats.
  int slot = 0;
  for (auto *attribute : {&m_vertexAttribute0,
           &m_vertexAttribute1,
           &m_vertexAttribute2,
           &m_vertexAttribute3}) {
    const int index = slot++;
    if (!*attribute)
      continue;
    std::vector<float> probe;
    if ((*attribute)->size() != numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.attribute%d' has %zu entries but the curve geometry has "
          "%zu vertices - ignoring it",
          index,
          (*attribute)->size(),
          numVertices);
      *attribute = nullptr;
    } else if (!anariFloatsToComponents(**attribute, 3, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.attribute%d' on curve geometry has an element type this "
          "device cannot read (%s) - ignoring it",
          index,
          anari::toString((*attribute)->elementType()));
      *attribute = nullptr;
    }
  }

  // Without an index array every consecutive pair of vertices is a segment,
  // i.e. the whole array is one polyline.
  if (!m_index) {
    m_vertices.resize(numVertices);
    for (size_t i = 0; i < numVertices; ++i)
      m_vertices[i] = uint32_t(i);
    m_curveVertexCounts.push_back(int(numVertices));
    m_valid = true;
    return;
  }

  if (m_index->elementType() != ANARI_UINT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.index' on curve geometry must be ANARI_UINT32");
    return;
  }

  const auto *idx = m_index->beginAs<uint32_t>();
  const size_t numSegments = m_index->size();
  for (size_t i = 0; i < numSegments; ++i) {
    // A segment runs from the indexed vertex to the next one, so the last
    // vertex can never start one.
    if (size_t(idx[i]) + 1 >= numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index'[%zu] on curve geometry is out of range: a "
          "segment starting at %u needs vertex %u, and there are %zu",
          i,
          idx[i],
          idx[i] + 1,
          numVertices);
      m_vertices.clear();
      m_curveVertexCounts.clear();
      return;
    }

    const bool continues = i > 0 && idx[i] == idx[i - 1] + 1;
    if (continues) {
      m_vertices.push_back(idx[i] + 1);
      ++m_curveVertexCounts.back();
    } else {
      m_vertices.push_back(idx[i]);
      m_vertices.push_back(idx[i] + 1);
      m_curveVertexCounts.push_back(2);
    }
  }

  m_valid = !m_curveVertexCounts.empty();
  if (!m_valid)
    reportMessage(ANARI_SEVERITY_WARNING, "the curve geometry has no segments");
}

bool Curve::isValid() const
{
  return m_valid;
}

const char *Curve::rdl2ClassName() const
{
  return "RdlCurveGeometry";
}

void Curve::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::FloatVector;
  using scene_rdl2::rdl2::IntVector;
  using scene_rdl2::rdl2::Vec3fVector;

  const auto *positions = m_vertexPosition->beginAs<float3>();

  Vec3fVector vertices;
  vertices.reserve(m_vertices.size());
  for (uint32_t i : m_vertices)
    vertices.emplace_back(positions[i].x, positions[i].y, positions[i].z);

  FloatVector radii;
  if (m_vertexRadius) {
    const auto *r = m_vertexRadius->beginAs<float>();
    radii.reserve(m_vertices.size());
    for (uint32_t i : m_vertices)
      radii.push_back(r[i]);
  } else {
    // One value is the constant rate; RdlCurveGeometry infers the rate from
    // how many values there are.
    radii.push_back(m_radius);
  }

  const IntVector counts(
      m_curveVertexCounts.begin(), m_curveVertexCounts.end());

  // 0 == linear: ANARI's curve is a piecewise *linear* polyline, so a bezier
  // or bspline reading of the same vertices would be a different curve.
  obj.set("curve_type", 0);
  // 1 == round: ANARI's curve is a tube (round segments joined by spheres -
  // Embree's round linear curve). MoonRay's default, "ray_facing", is a flat
  // ribbon turned towards each ray, which hides nothing behind the curve's
  // axis: anything inside the tube shows through, and it casts the shadow of
  // a ribbon (pynari sample07.py).
  obj.set("curves_subtype", 1);
  obj.set("curves_vertex_count", counts);
  obj.set("vertex_list_0", vertices);
  obj.set("radius_list", radii);

  // vertex.attribute0 is the texture coordinate, as on the triangle geometry:
  // what a hair shader looks the color of the scalp up with. Without it the
  // st MoonRay gives a curve stays, which runs along the curve and across it.
  // Always written, so that dropping the attribute really drops it.
  scene_rdl2::rdl2::Vec2fVector uvs;
  if (m_vertexAttribute0) {
    std::vector<float> values;
    if (anariFloatsToComponents(*m_vertexAttribute0, 2, values)) {
      uvs.reserve(m_vertices.size());
      for (uint32_t i : m_vertices)
        uvs.emplace_back(values[2 * size_t(i)], values[2 * size_t(i) + 1]);
    }
  }
  obj.set("uv_list", uvs);
}

namespace {

// A random number in [0, 1) for an index (the integer hash of Thomas Wang).
float hashToUnit(uint32_t key)
{
  key = (key ^ 61u) ^ (key >> 16);
  key *= 9u;
  key ^= key >> 4;
  key *= 0x27d4eb2du;
  key ^= key >> 15;
  return float(key >> 8) * (1.f / 16777216.f);
}

} // namespace

std::vector<Geometry::PrimitiveAttribute> Curve::primitiveAttributes() const
{
  std::vector<PrimitiveAttribute> attributes;

  if (m_vertexColor) {
    std::vector<float> rgb;
    if (anariColorsToRgb(*m_vertexColor, rgb)) {
      PrimitiveAttribute cd;
      cd.name = "Cd";
      cd.type = PrimitiveAttribute::Type::Color;
      cd.rate = 4; // vertex, in the flattened order built above
      cd.values.reserve(3 * m_vertices.size());
      for (uint32_t i : m_vertices) {
        cd.values.push_back(rgb[3 * size_t(i) + 0]);
        cd.values.push_back(rgb[3 * size_t(i) + 1]);
        cd.values.push_back(rgb[3 * size_t(i) + 2]);
      }
      attributes.push_back(std::move(cd));
    }
  }

  // The attribute slots, each a vec3f under a name of its own
  // (Geometry::attributeName()), in the flattened vertex order.
  int slot = 0;
  for (const auto *attribute : {&m_vertexAttribute0,
           &m_vertexAttribute1,
           &m_vertexAttribute2,
           &m_vertexAttribute3}) {
    const int index = slot++;
    if (!*attribute)
      continue;
    std::vector<float> values;
    if (!anariFloatsToComponents(**attribute, 3, values))
      continue;
    PrimitiveAttribute a;
    a.name = attributeName(index);
    a.type = PrimitiveAttribute::Type::Vec3f;
    a.rate = 4; // vertex
    a.values.reserve(3 * m_vertices.size());
    for (uint32_t i : m_vertices) {
      a.values.push_back(values[3 * size_t(i) + 0]);
      a.values.push_back(values[3 * size_t(i) + 1]);
      a.values.push_back(values[3 * size_t(i) + 2]);
    }
    attributes.push_back(std::move(a));
  }

  // What a hair shader asks its strand for (Curve.h).
  PrimitiveAttribute intercept, length, random, isCurve;
  intercept.name = "anari_curve_intercept";
  intercept.type = PrimitiveAttribute::Type::Float;
  intercept.rate = 4; // vertex
  intercept.values.reserve(m_vertices.size());
  length.name = "anari_curve_length";
  length.type = PrimitiveAttribute::Type::Float;
  length.rate = 3; // uniform: one per curve
  length.values.reserve(m_curveVertexCounts.size());
  random.name = "anari_curve_random";
  random.type = PrimitiveAttribute::Type::Float;
  random.rate = 3;
  random.values.reserve(m_curveVertexCounts.size());
  isCurve.name = "anari_is_curve";
  isCurve.type = PrimitiveAttribute::Type::Float;
  isCurve.rate = 1; // constant
  isCurve.values.push_back(1.f);

  const auto *positions = m_vertexPosition->beginAs<float3>();
  size_t first = 0;
  uint32_t curveIndex = 0;
  for (const int count : m_curveVertexCounts) {
    float total = 0.f;
    intercept.values.push_back(0.f);
    for (int k = 1; k < count; ++k) {
      const float3 a = positions[m_vertices[first + size_t(k) - 1]];
      const float3 b = positions[m_vertices[first + size_t(k)]];
      const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
      total += std::sqrt(dx * dx + dy * dy + dz * dz);
      intercept.values.push_back(total);
    }
    for (int k = 0; k < count; ++k) {
      float &value = intercept.values[first + size_t(k)];
      // A curve without length: by the index of the vertex.
      value = total > 0.f ? value / total : float(k) / float(count - 1);
    }
    length.values.push_back(total);
    random.values.push_back(hashToUnit(curveIndex++));
    first += size_t(count);
  }
  attributes.push_back(std::move(intercept));
  attributes.push_back(std::move(length));
  attributes.push_back(std::move(random));
  attributes.push_back(std::move(isCurve));

  return attributes;
}

} // namespace moonray_anari
