// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Perspective.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneClass.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/SceneVariables.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <cmath>

namespace moonray_anari {

// MoonRay's film-back width in mm. Kept at the PerspectiveCamera default: it
// only fixes the units in which the focal length is expressed, and the pair
// (focal, film_width_aperture) is what determines the frustum.
static constexpr float kFilmWidthMM = 24.f;

Perspective::Perspective(MoonrayDeviceGlobalState *s) : Camera(s) {}

void Perspective::commitParameters()
{
  Camera::commitParameters();
  m_fovy = getParam<float>("fovy", 1.0471976f);
  if (hasParam("aspect"))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
  m_near = getParam<float>("near", 1e-3f);
  m_far = getParam<float>("far", 1e6f);
  m_apertureRadius = getParam<float>("apertureRadius", 0.f);
  m_focusDistance = getParam<float>("focusDistance", 1.f);
}

bool Perspective::isValid() const
{
  return m_fovy > 0.f && m_fovy < 3.14159265f && m_near > 0.f
      && m_far > m_near;
}

const char *Perspective::rdl2ClassName() const
{
  return "PerspectiveCamera";
}

void Perspective::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, float imageAspect) const
{
  syncTransform(obj);

  // MoonRay's aperture window is [-1, -1/a, 1, 1/a] with a = image width /
  // height, scaled by film_width_aperture/2 and divided by pixel_aspect_ratio
  // in y (PerspectiveCamera::computeProjectionMatrix), so
  //
  //     tan(fovx/2) = (filmWidth/2) / focal
  //     tan(fovy/2) = (filmWidth/2) / (a * par * focal)
  //
  // ANARI defines fovy as the vertical field of view and aspect as the image
  // plane's width/height. Solving the first equation for the requested
  // horizontal field of view, tan(fovx/2) = aspect * tan(fovy/2), gives the
  // focal length; the second then fixes par so the vertical angle stays fovy
  // even when the frame's aspect differs from the camera's (par == 1 in the
  // usual case where they agree).
  const float aspect = m_aspect.value_or(imageAspect);
  const float tanHalfFovy = std::tan(0.5f * m_fovy);
  const float focal = (0.5f * kFilmWidthMM) / (aspect * tanHalfFovy);
  const float par = imageAspect > 0.f ? aspect / imageAspect : 1.f;

  obj.set("focal", focal);
  obj.set("film_width_aperture", kFilmWidthMM);
  obj.set("pixel_aspect_ratio", par);
  obj.set("near", m_near);
  obj.set("far", m_far);

  // Depth of field. MoonRay describes the lens photographically - an f-stop
  // against the focal length in mm - and turns it back into a world-space
  // lens radius through the scene scale (ProjectiveCamera::updateImpl):
  //
  //     radius = focal / (2 * fstop) / (scene_scale * 1000)
  //
  // so ANARI's apertureRadius becomes the f-stop that yields exactly that
  // radius for the focal length chosen above.
  const bool dof = m_apertureRadius > 0.f && m_focusDistance > 0.f;
  obj.set("dof", dof);
  if (dof) {
    using scene_rdl2::rdl2::SceneVariables;
    const SceneVariables &vars =
        obj.getSceneClass().getSceneContext()->getSceneVariables();
    const float sceneScale = vars.get(SceneVariables::sSceneScaleKey);
    const float worldToMm = sceneScale * 1000.f;
    obj.set("dof_aperture", focal / (2.f * m_apertureRadius * worldToMm));
    obj.set("dof_focus_distance", m_focusDistance);
  }

  // The renderer's own frustum comes from focal/film width; the projection
  // matrix path (dicing camera, motion vectors) is the only consumer of
  // near/far, and both are ANARI parameters with the same meaning.
}

float2 Perspective::imagePlaneHalfExtent(float imageAspect) const
{
  const float aspect = m_aspect.value_or(imageAspect);
  const float tanHalfFovy = std::tan(0.5f * m_fovy);
  return float2(aspect * tanHalfFovy, tanHalfFovy);
}

} // namespace moonray_anari
