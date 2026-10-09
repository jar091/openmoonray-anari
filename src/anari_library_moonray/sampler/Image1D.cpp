// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image1D.h"
#include "geometry/Geometry.h" // Geometry::attribute0Name
#include "texture/ScratchTexture.h"

// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Image1D::Image1D(MoonrayDeviceGlobalState *s) : Sampler(s), m_image(this) {}

Image1D::~Image1D()
{
  texture::removeScratchTexture(m_texturePath);
}

void Image1D::commitParameters()
{
  m_image = getParamObject<Array1D>("image");
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_filter = getParamString("filter", "linear");
  m_wrapMode = getParamString("wrapMode", "clampToEdge");

  // The coordinate is the first component of the (transformed) attribute:
  // u' = inTransform[0][0] * u + inTransform[3][0] + inOffset.x. The other
  // input components of a wider attribute are not read, so the rest of the
  // matrix's first row has nothing to act on.
  const mat4 t = getParam<mat4>("inTransform", mat4(linalg::identity));
  const float4 inOffset = getParam<float4>("inOffset", float4(0.f));
  m_uScale = t[0][0];
  m_uOffset = t[3][0] + inOffset.x;

  if (hasParam("outTransform") || hasParam("outOffset")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'outTransform'/'outOffset' are not supported by the image1D sampler "
        "yet - the image values are used unchanged");
  }
}

void Image1D::finalize()
{
  m_valid = false;

  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'image' on the image1D sampler");
    return;
  }

  if (m_inAttribute != "attribute0") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image1D sampler only reads 'attribute0' (the geometry's "
        "vertex.attribute0); '%s' is not available",
        m_inAttribute.c_str());
    return;
  }

  if (m_filter != "linear" && m_filter != "nearest") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'filter' \"%s\" on the image1D sampler - using linear",
        m_filter.c_str());
    m_filter = "linear";
  }

  if (m_wrapMode != "clampToEdge" && m_wrapMode != "repeat") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "wrap mode \"%s\" is not supported by the image1D sampler (MoonRay's "
        "ImageMap only repeats or clamps) - using clampToEdge",
        m_wrapMode.c_str());
    m_wrapMode = "clampToEdge";
  }

  m_valid = writeTextureFile();
}

bool Image1D::writeTextureFile()
{
  // filter = "nearest" is emulated as for image2D: every texel becomes a run
  // of identical pixels, so the texture system's filtering only blurs the
  // outermost pixel of each run.
  const uint32_t replicate = m_filter == "nearest"
      ? texture::nearestReplication(m_image->size())
      : 1u;

  std::string error;
  const std::string path = texture::writeScratchTexture("image1d",
      this,
      m_image->size(),
      1,
      m_image->elementType(),
      m_image->data(),
      /*channels=*/4,
      /*mipmapped=*/true,
      error,
      replicate,
      1);

  if (path.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image1D sampler could not prepare its texture: %s",
        error.c_str());
    return false;
  }

  texture::removeScratchTexture(m_texturePath);
  m_texturePath = path;
  return true;
}

bool Image1D::isValid() const
{
  return m_valid;
}

const char *Image1D::rdl2ClassName() const
{
  return "ImageMap";
}

void Image1D::syncRdl2(scene_rdl2::rdl2::SceneObject &obj)
{
  obj.set("texture", m_texturePath);
  // 2 == "input texture coordinates": st comes from the AttributeMap bound
  // to input_texture_coordinates (inputPrimitiveAttribute()). Its t is
  // irrelevant - the texture is a single row.
  obj.set("texture_coordinates", 2);
  // The scratch file is linear float, so no decoding on top of it.
  obj.set("gamma", 0);
  obj.set("wrap_around", m_wrapMode == "repeat");
  obj.set("scale", scene_rdl2::rdl2::Vec2f(m_uScale, 1.f));
  obj.set("offset", scene_rdl2::rdl2::Vec2f(m_uOffset, 0.f));
  obj.set("rotation_angle", 0.f);
}

const char *Image1D::inputPrimitiveAttribute() const
{
  return Geometry::attribute0Name;
}

} // namespace moonray_anari
