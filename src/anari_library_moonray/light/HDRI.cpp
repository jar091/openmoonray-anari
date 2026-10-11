// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HDRI.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <cmath>
#include <string>

namespace moonray_anari {

HDRI::HDRI(MoonrayDeviceGlobalState *s) : Light(s), m_radiance(this) {}

void HDRI::commitParameters()
{
  Light::commitParameters();
  m_radiance = getParamObject<Array2D>("radiance");
  m_up = getParam<float3>("up", float3(0.f, 0.f, 1.f));
  m_direction = getParam<float3>("direction", float3(1.f, 0.f, 0.f));
  m_scale = getParam<float>("scale", 1.f);
  m_visible = getParam<bool>("visible", true);

  const std::string layout = getParamString("layout", "equirectangular");
  if (layout != "equirectangular") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the hdri light only supports the \"equirectangular\" layout, not "
        "\"%s\"",
        layout.c_str());
  }
}

void HDRI::finalize()
{
  m_valid = false;

  if (!m_radiance) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'radiance' on the hdri light");
    return;
  }

  if (linalg::length(m_up) <= 0.f || linalg::length(m_direction) <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'up' and 'direction' of the hdri light must not be zero");
    return;
  }
  if (linalg::length(linalg::cross(m_up, m_direction)) <= 1e-6f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'up' and 'direction' of the hdri light are parallel, so the "
        "orientation of the environment map is undefined");
    return;
  }

  m_valid = updateTexture();
}

bool HDRI::updateTexture()
{
  texture::ImageSource source;
  source.array = m_radiance.get();
  source.version = uint64_t(m_radiance->lastDataModified());
  source.width = m_radiance->size().x;
  source.height = m_radiance->size().y;
  source.type = m_radiance->elementType();
  source.data = m_radiance->data();

  texture::TextureLayout layout;
  // EnvLight's map is read as RGB (pbr::ImageDistribution converts whatever it
  // opens to three channels), so the texture gets three.
  layout.channels = 3;
  // EnvLight opens the image itself rather than going through the texture
  // system, so it needs no pyramid - and a pyramid that reaches 1x1 makes it
  // log a warning for every map it loads.
  layout.mipmapped = false;

  std::string error;
  texture::TexturePtr texture =
      texture::acquireTexture("hdri", source, layout, error);
  if (!texture) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the hdri light could not prepare its environment map: %s",
        error.c_str());
    return false;
  }

  m_texture = std::move(texture);
  return true;
}

bool HDRI::isValid() const
{
  return m_valid && m_scale > 0.f;
}

const char *HDRI::rdl2ClassName() const
{
  return "EnvLight";
}

void HDRI::syncRdl2(
    scene_rdl2::rdl2::SceneObject &obj, const mat4 &instanceXfm) const
{
  using scene_rdl2::rdl2::Mat4d;
  using scene_rdl2::rdl2::Rgb;
  using scene_rdl2::rdl2::Vec4d;

  auto transformVector = [&](const float3 &v) {
    const float4 t = linalg::mul(instanceXfm, float4(v.x, v.y, v.z, 0.f));
    return float3(t.x, t.y, t.z);
  };

  // The pole is kept exactly where the application put it and `direction` is
  // orthogonalized against it: a tilted horizon is the more visible error of
  // the two, and ANARI's own wording ("the direction to which the *center* of
  // the texture is mapped") is about the azimuth.
  const float3 up = linalg::normalize(transformVector(m_up));
  const float3 dir0 = transformVector(m_direction);
  const float3 dir =
      linalg::normalize(dir0 - linalg::dot(dir0, up) * up);

  // MoonRay composes the node transform with a fixed rotation before building
  // the light's sampling frame (EnvLight::sLocalOrientation), which leaves
  //
  //   frameX = -Z_node,  frameY = -X_node,  frameZ = +Y_node
  //
  // and the map is addressed as u = 1 - phi/2pi, v = 1 - theta/pi with
  // phi = atan2(local.y, local.x) and theta measured from frameZ. So the pole
  // of the map (v = 1, the top row of the image) is frameZ and its center
  // column (u = 1/2, phi = pi) is -frameX. Requiring frameZ = up and
  // -frameX = direction gives the node basis below; frameY then follows as
  // frameZ x frameX, which is what makes the third row consistent.
  const float3 xAxis = linalg::cross(up, dir);

  const Mat4d xform(Vec4d(xAxis.x, xAxis.y, xAxis.z, 0.0),
      Vec4d(up.x, up.y, up.z, 0.0),
      Vec4d(dir.x, dir.y, dir.z, 0.0),
      Vec4d(0.0, 0.0, 0.0, 1.0));

  obj.set("node_xform", xform);
  obj.set("texture", m_texture ? m_texture->name() : std::string());
  // An EnvLight's color is radiance directly (EnvLight::update()), and the
  // texture multiplies it, so ANARI's `scale` is the intensity and its `color`
  // the tint.
  obj.set("color", Rgb(m_color.x, m_color.y, m_color.z));
  obj.set("intensity", m_scale);
  obj.set("exposure", 0.f);
  obj.set("sample_upper_hemisphere_only", false);
  // EnvLight looks the map up nearest-neighbor by default, which shows every
  // texel of a low-resolution map as a hard-edged block - in the background
  // and in every mirror reflection. The other ANARI devices filter the map
  // bilinearly (1 == "bilinear").
  obj.set("texture_filter", 1);
  obj.set("visible_in_camera", m_visible ? 1 : 0);
}

} // namespace moonray_anari
