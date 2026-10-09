// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Triangle.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Triangle::Triangle(MoonrayDeviceGlobalState *s)
    : Geometry(s),
      m_vertexPosition(this),
      m_index(this),
      m_vertexNormal(this),
      m_vertexColor(this),
      m_vertexAttribute0(this)
{}

void Triangle::commitParameters()
{
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexNormal = getParamObject<Array1D>("vertex.normal");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");

  reportUnsupportedParameters({"vertex.attribute1",
      "vertex.attribute2",
      "vertex.attribute3",
      "vertex.tangent",
      "primitive.color",
      "primitive.attribute0",
      "primitive.attribute1",
      "primitive.attribute2",
      "primitive.attribute3",
      "faceVarying.normal",
      "faceVarying.color",
      "faceVarying.attribute0",
      "color",
      "attribute0",
      "attribute1",
      "attribute2",
      "attribute3"});
}

void Triangle::finalize()
{
  m_valid = false;

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on triangle geometry");
    return;
  }

  if (m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.position' on triangle geometry must be ANARI_FLOAT32_VEC3");
    return;
  }

  if (m_index && m_index->elementType() != ANARI_UINT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.index' on triangle geometry must be ANARI_UINT32_VEC3");
    return;
  }

  const size_t numVertices = m_vertexPosition->size();

  // An *optional* attribute this device cannot use is dropped with a warning
  // rather than failing the geometry. Only the parameters the surface cannot
  // exist without - vertex.position and a usable primitive.index - are fatal:
  // discarding a whole mesh because a secondary attribute had an unexpected
  // type makes the surface vanish from the render, which is a far worse
  // answer than rendering it without that attribute.
  if (m_vertexNormal) {
    if (m_vertexNormal->elementType() != ANARI_FLOAT32_VEC3) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.normal' on triangle geometry must be ANARI_FLOAT32_VEC3 - "
          "ignoring it and using the geometric normal");
      m_vertexNormal = nullptr;
    } else if (m_vertexNormal->size() != numVertices) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.normal' has %zu entries but the geometry has %zu vertices "
          "- ignoring it and using the geometric normal",
          m_vertexNormal->size(),
          numVertices);
      m_vertexNormal = nullptr;
    }
  }

  if (m_vertexColor) {
    std::vector<float> probe;
    if (!anariColorsToRgb(*m_vertexColor, probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.color' on triangle geometry has an element type this "
          "device cannot read (%s) - ignoring it",
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
    const ANARIDataType t = m_vertexAttribute0->elementType();
    if (t != ANARI_FLOAT32 && t != ANARI_FLOAT32_VEC2 && t != ANARI_FLOAT32_VEC3
        && t != ANARI_FLOAT32_VEC4) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'vertex.attribute0' on triangle geometry must be a float, float2, "
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

  if (!m_index && numVertices % 3 != 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "triangle geometry without 'primitive.index' needs a multiple of 3 "
        "vertices, got %zu",
        numVertices);
    return;
  }

  if (m_index) {
    // Out-of-range indices would be read by MoonRay as vertex data, so they
    // are rejected here rather than passed on (master prompt: treat API input
    // as untrusted).
    const auto *idx = m_index->beginAs<uint3>();
    const size_t numTriangles = m_index->size();
    for (size_t i = 0; i < numTriangles; ++i) {
      if (idx[i].x >= numVertices || idx[i].y >= numVertices
          || idx[i].z >= numVertices) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "'primitive.index'[%zu] on triangle geometry is out of range "
            "(%u vertices)",
            i,
            (unsigned)numVertices);
        return;
      }
    }
  }

  m_valid = true;
}

bool Triangle::isValid() const
{
  return m_valid;
}

const char *Triangle::rdl2ClassName() const
{
  return "RdlMeshGeometry";
}

void Triangle::syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const
{
  using scene_rdl2::rdl2::IntVector;
  using scene_rdl2::rdl2::Vec3f;
  using scene_rdl2::rdl2::Vec3fVector;

  const size_t numVertices = m_vertexPosition->size();
  const auto *positions = m_vertexPosition->beginAs<float3>();

  Vec3fVector vertices;
  vertices.reserve(numVertices);
  for (size_t i = 0; i < numVertices; ++i)
    vertices.emplace_back(positions[i].x, positions[i].y, positions[i].z);

  const size_t numTriangles = m_index ? m_index->size() : numVertices / 3;

  IntVector indices;
  indices.reserve(numTriangles * 3);
  if (m_index) {
    const auto *idx = m_index->beginAs<uint3>();
    for (size_t i = 0; i < numTriangles; ++i) {
      indices.push_back(static_cast<int>(idx[i].x));
      indices.push_back(static_cast<int>(idx[i].y));
      indices.push_back(static_cast<int>(idx[i].z));
    }
  } else {
    for (size_t i = 0; i < numTriangles * 3; ++i)
      indices.push_back(static_cast<int>(i));
  }

  const IntVector faceVertexCount(numTriangles, 3);

  obj.set("vertex_list_0", vertices);
  obj.set("vertices_by_index", indices);
  obj.set("face_vertex_count", faceVertexCount);

  // vertex.attribute0 is the texture coordinate set: RdlMesh turns a
  // vertex-rate uv_list into the `st` primitive attribute, which is what
  // ImageMap samples (docs/ANARI_MOONRAY_MAPPING.md). Only the first two
  // components are texture coordinates; ANARI allows the slot to be wider.
  if (m_vertexAttribute0) {
    scene_rdl2::rdl2::Vec2fVector uvs;
    uvs.reserve(numVertices);
    switch (m_vertexAttribute0->elementType()) {
    case ANARI_FLOAT32: {
      // A one-component attribute is the u coordinate; v is 0, which is what
      // ANARI's attribute-widening rule (x, 0, 0, 1) gives.
      const auto *u = m_vertexAttribute0->beginAs<float>();
      for (size_t i = 0; i < numVertices; ++i)
        uvs.emplace_back(u[i], 0.f);
      break;
    }
    case ANARI_FLOAT32_VEC2: {
      const auto *uv = m_vertexAttribute0->beginAs<float2>();
      for (size_t i = 0; i < numVertices; ++i)
        uvs.emplace_back(uv[i].x, uv[i].y);
      break;
    }
    case ANARI_FLOAT32_VEC3: {
      const auto *uv = m_vertexAttribute0->beginAs<float3>();
      for (size_t i = 0; i < numVertices; ++i)
        uvs.emplace_back(uv[i].x, uv[i].y);
      break;
    }
    default: {
      const auto *uv = m_vertexAttribute0->beginAs<float4>();
      for (size_t i = 0; i < numVertices; ++i)
        uvs.emplace_back(uv[i].x, uv[i].y);
      break;
    }
    }
    obj.set("uv_list", uvs);
  }
  // Shading normals. RdlMesh takes them vertex-rate in `normal_list`, in the
  // same space as the vertices, and uses them instead of the geometric normal
  // for shading (the geometric normal still decides what the ray hit).
  if (m_vertexNormal) {
    const auto *normals = m_vertexNormal->beginAs<float3>();
    Vec3fVector normalList;
    normalList.reserve(numVertices);
    for (size_t i = 0; i < numVertices; ++i)
      normalList.emplace_back(normals[i].x, normals[i].y, normals[i].z);
    obj.set("normal_list", normalList);
  }

  // RdlMeshGeometry defaults to a subdivision cage; an ANARI triangle mesh is
  // the polygon mesh itself.
  obj.set("is_subd", false);
  // Without 'vertex.normal' ANARI shades with the geometric normal. MoonRay
  // would instead average the face normals around every vertex
  // (smooth_normal defaults to true) - and where two triangles sharing a
  // vertex are wound oppositely those averages cancel, which turns the
  // shading normal to zero and the triangle black (pynari
  // sample01_with_texture.py).
  obj.set("smooth_normal", false);
}

std::vector<Geometry::PrimitiveAttribute> Triangle::primitiveAttributes() const
{
  std::vector<PrimitiveAttribute> attributes;

  // `vertex.color` becomes MoonRay's own vertex-color attribute, "Cd", which
  // a material reads through an AttributeMap when its color is set to the
  // string "color" (docs/ANARI_MOONRAY_MAPPING.md).
  if (m_vertexColor) {
    PrimitiveAttribute cd;
    cd.name = "Cd";
    cd.type = PrimitiveAttribute::Type::Color;
    cd.rate = 4; // vertex
    if (anariColorsToRgb(*m_vertexColor, cd.values))
      attributes.push_back(std::move(cd));
  }

  // vertex.attribute0 is also the mesh's st (uv_list, above); the generic
  // copy is what an image1D sampler reads.
  if (m_vertexAttribute0) {
    PrimitiveAttribute a0;
    a0.name = attribute0Name;
    a0.type = PrimitiveAttribute::Type::Vec2f;
    a0.rate = 4; // vertex
    if (anariFloatsToComponents(*m_vertexAttribute0, 2, a0.values))
      attributes.push_back(std::move(a0));
  }

  return attributes;
}

} // namespace moonray_anari
