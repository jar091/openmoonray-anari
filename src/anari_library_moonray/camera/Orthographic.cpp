// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Orthographic.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Orthographic::Orthographic(MoonrayDeviceGlobalState *s) : Camera(s) {}

void Orthographic::commitParameters()
{
  Camera::commitParameters();
  m_height = getParam<float>("height", 1.f);
  if (hasParam("aspect"))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
  m_near = getParam<float>("near", 1e-3f);
  m_far = getParam<float>("far", 1e6f);
}

bool Orthographic::isValid() const
{
  return m_height > 0.f && m_far > m_near;
}

const char *Orthographic::rdl2ClassName() const
{
  return "OrthographicCamera";
}

void Orthographic::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, float imageAspect) const
{
  syncTransform(obj);

  // The aperture window is [-1, -1/a, 1, 1/a] scaled by film_width_aperture/2
  // (and divided by pixel_aspect_ratio in y), and for an orthographic camera
  // those are world units rather than a focal-length ratio. So the visible
  // width *is* film_width_aperture, and
  //
  //     width  = height * aspect
  //     par    = aspect / imageAspect
  //
  // which keeps the vertical extent at `height` even when the frame's aspect
  // differs from the camera's - the same correction the perspective camera
  // makes for its vertical field of view.
  //
  // `imageRegion` selects a part of that rectangle (Camera::film()), in the
  // same world units.
  const float aspect = m_aspect.value_or(imageAspect);
  const Film world = film(m_height * aspect, m_height, imageAspect);

  obj.set("film_width_aperture", world.width);
  obj.set("pixel_aspect_ratio", world.pixelAspectRatio);
  obj.set("horizontal_film_offset", world.offsetX);
  obj.set("vertical_film_offset", world.offsetY);
  obj.set("near", m_near);
  obj.set("far", m_far);
}

} // namespace moonray_anari
