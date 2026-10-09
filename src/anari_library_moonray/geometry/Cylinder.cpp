// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Cylinder.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <algorithm>
#include <cmath>

namespace moonray_anari {

namespace {

// Segments around the axis. The side normals are analytic, so shading is
// smooth at any count; the count only shows in the silhouette, and it comes
// down for scenes with very many primitives to keep the mesh affordable.
int segmentCount(size_t numPrimitives)
{
  if (numPrimitives <= 1024)
    return 64;
  if (numPrimitives <= 65536)
    return 32;
  return 16;
}

// Two unit vectors completing `w` to a right-handed orthonormal basis
// (u x v == w); Duff et al., "Building an Orthonormal Basis, Revisited".
void orthonormalBasis(const float3 &w, float3 &u, float3 &v)
{
  const float sign = std::copysign(1.f, w.z);
  const float a = -1.f / (sign + w.z);
  const float b = w.x * w.y * a;
  u = float3(1.f + sign * w.x * w.x * a, sign * b, -sign * w.x);
  v = float3(b, sign + w.y * w.y * a, -w.y);
}

} // namespace

Cylinder::Cylinder(MoonrayDeviceGlobalState *s, bool cone)
    : Geometry(s),
      m_cone(cone),
      m_vertexPosition(this),
      m_index(this),
      m_primitiveRadius(this),
      m_vertexRadius(this),
      m_vertexCap(this),
      m_vertexColor(this),
      m_primitiveColor(this),
      m_vertexAttribute0(this),
      m_primitiveAttribute0(this)
{}

const char *Cylinder::subtypeName() const
{
  return m_cone ? "cone" : "cylinder";
}

void Cylinder::commitParameters()
{
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexCap = getParamObject<Array1D>("vertex.cap");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_primitiveColor = getParamObject<Array1D>("primitive.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_primitiveAttribute0 = getParamObject<Array1D>("primitive.attribute0");
  if (m_cone) {
    m_vertexRadius = getParamObject<Array1D>("vertex.radius");
    m_primitiveRadius = nullptr;
  } else {
    m_primitiveRadius = getParamObject<Array1D>("primitive.radius");
    m_vertexRadius = nullptr;
    m_radius = getParam<float>("radius", 1.f);
  }
  m_caps = getParamString("caps", "none");
  m_hasConstantColor = getParam("color", ANARI_FLOAT32_VEC4, &m_color);
  if (!m_hasConstantColor) {
    float3 c(1.f, 1.f, 1.f);
    m_hasConstantColor = getParam("color", ANARI_FLOAT32_VEC3, &c);
    m_color = float4(c.x, c.y, c.z, 1.f);
  }

  reportUnsupportedParameters({"vertex.attribute1",
      "vertex.attribute2",
      "vertex.attribute3",
      "primitive.attribute1",
      "primitive.attribute2",
      "primitive.attribute3",
      "attribute0",
      "attribute1",
      "attribute2",
      "attribute3"});
}

void Cylinder::finalize()
{
  m_valid = false;
  m_positions.clear();
  m_normals.clear();
  m_indices.clear();
  m_colors.clear();
  m_attribute0.clear();

  const char *name = subtypeName();

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on %s geometry",
        name);
    return;
  }
  if (m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.position' on %s geometry must be ANARI_FLOAT32_VEC3",
        name);
    return;
  }
  const size_t numVertices = m_vertexPosition->size();

  // --- primitives ---------------------------------------------------------
  std::vector<uint2> primitives;
  if (m_index) {
    const ANARIDataType t = m_index->elementType();
    if (t != ANARI_UINT32_VEC2 && t != ANARI_UINT64_VEC2) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' on %s geometry must be ANARI_UINT32_VEC2 or "
          "ANARI_UINT64_VEC2",
          name);
      return;
    }
    primitives.reserve(m_index->size());
    for (size_t i = 0; i < m_index->size(); ++i) {
      uint64_t a, b;
      if (t == ANARI_UINT32_VEC2) {
        const uint2 idx = m_index->beginAs<uint2>()[i];
        a = idx.x;
        b = idx.y;
      } else {
        const uint64_t *idx = static_cast<const uint64_t *>(m_index->data());
        a = idx[2 * i];
        b = idx[2 * i + 1];
      }
      if (a >= numVertices || b >= numVertices) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "'primitive.index'[%zu] on %s geometry is out of range (%zu "
            "vertices)",
            i,
            name,
            numVertices);
        return;
      }
      primitives.emplace_back(uint32_t(a), uint32_t(b));
    }
  } else {
    // Without an index, consecutive vertex pairs are the primitives.
    for (size_t i = 0; i + 1 < numVertices; i += 2)
      primitives.emplace_back(uint32_t(i), uint32_t(i + 1));
  }

  if (primitives.empty()) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "the %s geometry has no primitives", name);
    return;
  }
  const size_t numPrimitives = primitives.size();

  // --- radii --------------------------------------------------------------
  if (m_cone) {
    if (!m_vertexRadius) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'vertex.radius' on cone geometry");
      return;
    }
    if (m_vertexRadius->elementType() != ANARI_FLOAT32
        || m_vertexRadius->size() != numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.radius' on cone geometry must be an ANARI_FLOAT32 array "
          "with one entry per vertex");
      return;
    }
  } else if (m_primitiveRadius) {
    if (m_primitiveRadius->elementType() != ANARI_FLOAT32
        || m_primitiveRadius->size() != numPrimitives) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.radius' on cylinder geometry must be an ANARI_FLOAT32 "
          "array with one entry per primitive - ignoring it and using the "
          "constant 'radius'");
      m_primitiveRadius = nullptr;
    }
  }
  if (!m_cone && !m_primitiveRadius && !(m_radius > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'radius' on cylinder geometry must be positive, got %f",
        m_radius);
    return;
  }

  // --- optional attributes: dropped with a warning, never fatal -----------
  auto checkColors = [&](helium::ChangeObserverPtr<Array1D> &array,
                         const char *param,
                         size_t expected) {
    if (!array)
      return;
    std::vector<float> probe;
    if (!anariColorsToRgb(*array, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' on %s geometry has an element type this device cannot read "
          "(%s) - ignoring it",
          param,
          name,
          anari::toString(array->elementType()));
      array = nullptr;
    } else if (array->size() != expected) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' on %s geometry has %zu entries, expected %zu - ignoring it",
          param,
          name,
          array->size(),
          expected);
      array = nullptr;
    }
  };
  checkColors(m_vertexColor, "vertex.color", numVertices);
  checkColors(m_primitiveColor, "primitive.color", numPrimitives);

  auto checkFloats = [&](helium::ChangeObserverPtr<Array1D> &array,
                         const char *param,
                         size_t expected) {
    if (!array)
      return;
    std::vector<float> probe;
    if (!anariFloatsToComponents(*array, 2, probe)
        || array->size() != expected) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' on %s geometry must be a float, float2, float3 or float4 "
          "array with %zu entries - ignoring it",
          param,
          name,
          expected);
      array = nullptr;
    }
  };
  checkFloats(m_vertexAttribute0, "vertex.attribute0", numVertices);
  checkFloats(m_primitiveAttribute0, "primitive.attribute0", numPrimitives);

  if (m_vertexCap
      && (m_vertexCap->elementType() != ANARI_UINT8
          || m_vertexCap->size() != numVertices)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.cap' on %s geometry must be an ANARI_UINT8 array with one "
        "entry per vertex - ignoring it",
        name);
    m_vertexCap = nullptr;
  }

  if (m_caps != "none" && m_caps != "first" && m_caps != "second"
      && m_caps != "both") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'caps' \"%s\" on %s geometry - using \"none\"",
        m_caps.c_str(),
        name);
    m_caps = "none";
  }

  tessellate(primitives);

  m_valid = !m_indices.empty();
  if (!m_valid) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "every primitive of the %s geometry is degenerate (zero length or "
        "zero radius)",
        name);
  }
}

void Cylinder::tessellate(const std::vector<uint2> &primitives)
{
  const auto *positions = m_vertexPosition->beginAs<float3>();
  const float *vertexRadius =
      m_vertexRadius ? m_vertexRadius->beginAs<float>() : nullptr;
  const float *primitiveRadius =
      m_primitiveRadius ? m_primitiveRadius->beginAs<float>() : nullptr;
  const uint8_t *vertexCap =
      m_vertexCap ? m_vertexCap->beginAs<uint8_t>() : nullptr;

  std::vector<float> vertexColors, primitiveColors;
  if (m_vertexColor)
    anariColorsToRgb(*m_vertexColor, vertexColors);
  else if (m_primitiveColor)
    anariColorsToRgb(*m_primitiveColor, primitiveColors);
  const bool hasColor =
      m_vertexColor || m_primitiveColor || m_hasConstantColor;

  std::vector<float> vertexUv, primitiveUv;
  if (m_vertexAttribute0)
    anariFloatsToComponents(*m_vertexAttribute0, 2, vertexUv);
  else if (m_primitiveAttribute0)
    anariFloatsToComponents(*m_primitiveAttribute0, 2, primitiveUv);
  const bool hasUv = m_vertexAttribute0 || m_primitiveAttribute0;

  const int n = segmentCount(primitives.size());
  const float twoPi = 6.28318530717958647692f;
  std::vector<float> cosines(n), sines(n);
  for (int j = 0; j < n; ++j) {
    cosines[j] = std::cos(twoPi * float(j) / float(n));
    sines[j] = std::sin(twoPi * float(j) / float(n));
  }

  for (size_t prim = 0; prim < primitives.size(); ++prim) {
    const uint32_t ends[2] = {primitives[prim].x, primitives[prim].y};
    const float3 p0 = positions[ends[0]];
    const float3 p1 = positions[ends[1]];
    float radius[2];
    for (int e = 0; e < 2; ++e) {
      radius[e] = vertexRadius ? vertexRadius[ends[e]]
                               : (primitiveRadius ? primitiveRadius[prim]
                                                  : m_radius);
      radius[e] = std::max(radius[e], 0.f);
    }

    const float3 axis = p1 - p0;
    const float length = linalg::length(axis);
    if (!(length > 0.f) || (radius[0] <= 0.f && radius[1] <= 0.f))
      continue;
    const float3 w = axis / length;
    float3 u, v;
    orthonormalBasis(w, u, v);

    // Per-end values of the attributes.
    float3 color[2];
    float2 uv[2];
    for (int e = 0; e < 2; ++e) {
      if (!vertexColors.empty()) {
        const float *c = vertexColors.data() + 3 * size_t(ends[e]);
        color[e] = float3(c[0], c[1], c[2]);
      } else if (!primitiveColors.empty()) {
        const float *c = primitiveColors.data() + 3 * prim;
        color[e] = float3(c[0], c[1], c[2]);
      } else {
        color[e] = float3(m_color.x, m_color.y, m_color.z);
      }
      if (!vertexUv.empty())
        uv[e] = float2(vertexUv[2 * ends[e]], vertexUv[2 * ends[e] + 1]);
      else if (!primitiveUv.empty())
        uv[e] = float2(primitiveUv[2 * prim], primitiveUv[2 * prim + 1]);
      else
        uv[e] = float2(0.f, 0.f);
    }

    auto addVertex = [&](const float3 &p, const float3 &normal, int end) {
      m_positions.push_back(p);
      m_normals.push_back(normal);
      if (hasColor) {
        m_colors.push_back(color[end].x);
        m_colors.push_back(color[end].y);
        m_colors.push_back(color[end].z);
      }
      if (hasUv) {
        m_attribute0.push_back(uv[end].x);
        m_attribute0.push_back(uv[end].y);
      }
      return int(m_positions.size() - 1);
    };

    // --- side: two rings, normal tilted by the cone's slope ---------------
    // A surface point is p0 + t*length*w + r(t)*radial with r linear from
    // radius[0] to radius[1]; radial*length - w*(r1 - r0) is orthogonal to
    // both of its tangents.
    const int side = int(m_positions.size());
    for (int j = 0; j < n; ++j) {
      const float3 radial = cosines[j] * u + sines[j] * v;
      const float3 normal =
          linalg::normalize(radial * length - w * (radius[1] - radius[0]));
      addVertex(p0 + radius[0] * radial, normal, 0);
      addVertex(p1 + radius[1] * radial, normal, 1);
    }
    for (int j = 0; j < n; ++j) {
      const int k = (j + 1) % n;
      const int a0 = side + 2 * j, a1 = a0 + 1;
      const int b0 = side + 2 * k, b1 = b0 + 1;
      // Counter-clockwise seen from outside (u x v == w).
      m_indices.insert(m_indices.end(), {a0, b0, b1, a0, b1, a1});
    }

    // --- caps: flat disks facing away from the other end -------------------
    for (int e = 0; e < 2; ++e) {
      bool cap = vertexCap
          ? vertexCap[ends[e]] != 0
          : (m_caps == "both" || (e == 0 && m_caps == "first")
              || (e == 1 && m_caps == "second"));
      if (!cap || radius[e] <= 0.f)
        continue;
      const float3 center = e == 0 ? p0 : p1;
      const float3 normal = e == 0 ? -w : w;
      const int c = addVertex(center, normal, e);
      for (int j = 0; j < n; ++j) {
        const float3 radial = cosines[j] * u + sines[j] * v;
        addVertex(center + radius[e] * radial, normal, e);
      }
      for (int j = 0; j < n; ++j) {
        const int a = c + 1 + j;
        const int b = c + 1 + (j + 1) % n;
        if (e == 0)
          m_indices.insert(m_indices.end(), {c, b, a});
        else
          m_indices.insert(m_indices.end(), {c, a, b});
      }
    }
  }
}

bool Cylinder::isValid() const
{
  return m_valid;
}

const char *Cylinder::rdl2ClassName() const
{
  return "RdlMeshGeometry";
}

void Cylinder::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::IntVector;
  using scene_rdl2::rdl2::Vec3fVector;

  Vec3fVector vertices;
  vertices.reserve(m_positions.size());
  for (const auto &p : m_positions)
    vertices.emplace_back(p.x, p.y, p.z);

  Vec3fVector normals;
  normals.reserve(m_normals.size());
  for (const auto &nrm : m_normals)
    normals.emplace_back(nrm.x, nrm.y, nrm.z);

  const IntVector indices(m_indices.begin(), m_indices.end());
  const IntVector faceVertexCount(m_indices.size() / 3, 3);

  obj.set("vertex_list_0", vertices);
  obj.set("vertices_by_index", indices);
  obj.set("face_vertex_count", faceVertexCount);
  obj.set("normal_list", normals);
  obj.set("is_subd", false);
  obj.set("smooth_normal", false);
}

std::vector<Geometry::PrimitiveAttribute> Cylinder::primitiveAttributes() const
{
  std::vector<PrimitiveAttribute> attributes;

  if (!m_colors.empty()) {
    PrimitiveAttribute cd;
    cd.name = "Cd";
    cd.type = PrimitiveAttribute::Type::Color;
    cd.rate = 4; // vertex
    cd.values = m_colors;
    attributes.push_back(std::move(cd));
  }

  if (!m_attribute0.empty()) {
    PrimitiveAttribute a0;
    a0.name = attribute0Name;
    a0.type = PrimitiveAttribute::Type::Vec2f;
    a0.rate = 4; // vertex
    a0.values = m_attribute0;
    attributes.push_back(std::move(a0));
  }

  return attributes;
}

} // namespace moonray_anari
