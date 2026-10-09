// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "array/Array1D.h"
// std
#include <cstdint>
#include <vector>

namespace moonray_anari {

// ANARI 'sphere' geometry -> MoonRay RdlPointGeometry.
//
// MoonRay's "points" *are* spheres: each entry of `vertex_list_0` is a sphere
// centered there, with the radius from `radius_list` (one value is read as a
// constant rate, one per point as per-point). ANARI's `primitive.index`
// selects which vertices are spheres, so the device resolves it here rather
// than passing a second array MoonRay has no equivalent for.
struct Sphere : public Geometry
{
  Sphere(MoonrayDeviceGlobalState *s);
  ~Sphere() override = default;

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
  helium::ChangeObserverPtr<Array1D> m_vertexAttribute0;
  helium::ChangeObserverPtr<Array1D> m_primitiveColor;
  helium::ChangeObserverPtr<Array1D> m_index;
  float m_radius{1.f};

  // Which source vertex each rendered sphere comes from - the identity when
  // there is no `primitive.index`.
  std::vector<uint32_t> m_points;
  bool m_valid{false};
};

} // namespace moonray_anari
