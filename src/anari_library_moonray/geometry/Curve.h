// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "array/Array1D.h"
// std
#include <cstdint>
#include <vector>

namespace moonray_anari {

// ANARI 'curve' geometry -> MoonRay RdlCurveGeometry with `curve_type` linear.
//
// The two descriptions of the same thing differ in shape: ANARI lists
// *segments* (each `primitive.index` entry is the first vertex of a two-vertex
// segment), MoonRay lists *curves* (`curves_vertex_count` plus one flat vertex
// buffer). Segments that continue one another are therefore merged back into
// polylines here, which is lossless and keeps the vertex buffer the size it
// was; a segment that stands alone becomes a two-vertex curve, the smallest
// linear curve MoonRay accepts.
struct Curve : public Geometry
{
  Curve(MoonrayDeviceGlobalState *s);
  ~Curve() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const override;
  std::vector<PrimitiveAttribute> primitiveAttributes() const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  helium::ChangeObserverPtr<Array1D> m_vertexColor;
  helium::ChangeObserverPtr<Array1D> m_index;
  float m_radius{1.f};

  // The flattened vertex buffer as source vertex indices, and how many of them
  // each curve owns. Built in finalize() so that syncRdl2() and the primitive
  // attributes agree on the layout.
  std::vector<uint32_t> m_vertices;
  std::vector<int> m_curveVertexCounts;
  bool m_valid{false};
};

} // namespace moonray_anari
