// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "array/Array1D.h"
// std
#include <cstdint>
#include <string>
#include <vector>

namespace moonray_anari {

// ANARI 'cylinder' and 'cone' geometry -> a tessellated MoonRay
// RdlMeshGeometry.
//
// MoonRay has no cylinder or cone primitive. Its round curves come closest,
// but Embree builds those with rounded joints and ends, while ANARI's
// cylinders and cones are open (caps = "none", the default) or closed by flat
// disks. So each primitive is tessellated into a triangle mesh instead: a ring
// of side faces with smooth (analytic) normals, plus a flat cap disk at every
// end that asks for one. A cone is a cylinder with a radius per end, so both
// subtypes share this class.
//
// Colors and attribute0 follow ANARI's precedence - per vertex, else per
// primitive, else the constant - and are written per mesh vertex: every
// primitive gets its own vertices, so a per-primitive value stays exactly
// that.
struct Cylinder : public Geometry
{
  // `cone` selects the cone's parameters: a radius per vertex rather than
  // one per primitive.
  Cylinder(MoonrayDeviceGlobalState *s, bool cone);
  ~Cylinder() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  const char *rdl2ClassName() const override;
  void syncRdl2(scene_rdl2::rdl2::SceneObject &obj) const override;
  std::vector<PrimitiveAttribute> primitiveAttributes() const override;

 private:
  const char *subtypeName() const;
  // Builds the mesh below from the current parameters.
  void tessellate(const std::vector<uint2> &primitives);

  const bool m_cone;

  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_primitiveRadius;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  helium::ChangeObserverPtr<Array1D> m_vertexCap;
  helium::ChangeObserverPtr<Array1D> m_vertexColor;
  helium::ChangeObserverPtr<Array1D> m_primitiveColor;
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute0;
  helium::ChangeObserverPtr<Array1D> m_primitiveAttribute0;
  float m_radius{1.f};
  std::string m_caps{"none"};
  bool m_hasConstantColor{false};
  float4 m_color{1.f, 1.f, 1.f, 1.f};

  // The tessellated mesh.
  std::vector<float3> m_positions;
  std::vector<float3> m_normals;
  std::vector<int> m_indices;
  std::vector<float> m_colors; // rgb per vertex, empty without a color
  std::vector<float> m_attribute0; // uv per vertex, empty without one
  bool m_valid{false};
};

} // namespace moonray_anari
