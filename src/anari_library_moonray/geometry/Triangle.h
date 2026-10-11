// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "array/Array1D.h"

namespace moonray_anari {

// ANARI 'triangle' geometry -> MoonRay RdlMeshGeometry with three-vertex
// faces and subdivision turned off (an ANARI triangle mesh is polygonal, not
// a subdivision cage).
//
// Consumes vertex.position, primitive.index, vertex.normal, vertex.color and
// vertex.attribute0 .. vertex.attribute3. attribute0 is also the mesh's
// texture coordinates (what an image2D sampler reads); all four slots are
// primitive attributes a shading network can name (Geometry::attributeName()).
// The remaining triangle parameters (primitive-rate and face-varying data,
// tangents) are deliberately not advertised until they are implemented and
// tested.
struct Triangle : public Geometry
{
  Triangle(MoonrayDeviceGlobalState *s);
  ~Triangle() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const override;
  std::vector<PrimitiveAttribute> primitiveAttributes() const override;
  float surfaceArea() const override;
  bool displacedPositions(
      float distance, std::vector<float> &positions) const override;
  float boundingRadius() const override;
  size_t triangleCount() const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexNormal;
  helium::ChangeObserverPtr<Array1D> m_vertexColor;
  helium::ChangeObserverPtr<Array1D> m_vertexTangent;
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute0;
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute1;
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute2;
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute3;
  bool m_valid{false};
};

} // namespace moonray_anari
