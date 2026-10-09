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
// vertex.attribute0 (the texture coordinates an image2D sampler reads). The
// remaining triangle parameters (the other attribute slots, primitive-rate and
// face-varying data) are deliberately not advertised until they are
// implemented and tested.
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

 private:
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexNormal;
  helium::ChangeObserverPtr<Array1D> m_vertexColor;
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute0;
  bool m_valid{false};
};

} // namespace moonray_anari
