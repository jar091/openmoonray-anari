// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image2D.h"

// anari
#include <anari/frontend/type_utility.h>
// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>
// std
#include <algorithm>
#include <cmath>
#include <string>

namespace moonray_anari {

Image2D::Image2D(MoonrayDeviceGlobalState *s) : Sampler(s), m_image(this) {}

void Image2D::commitParameters()
{
  m_image = getParamObject<Array2D>("image");
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_filter = getParamString("filter", "linear");
  m_wrapMode1 = getParamString("wrapMode1", "clampToEdge");
  m_wrapMode2 = getParamString("wrapMode2", "clampToEdge");

  // The 2D affine part of 'inTransform' and 'inOffset', applied to (u, v):
  // uv' = A * uv + b.
  const mat4 t = getParam<mat4>("inTransform", mat4(linalg::identity));
  const float4 inOffset = getParam<float4>("inOffset", float4(0.f));
  const float a00 = t[0][0], a01 = t[1][0], a10 = t[0][1], a11 = t[1][1];
  m_uvOffset[0] = t[3][0] + inOffset.x;
  m_uvOffset[1] = t[3][1] + inOffset.y;

  // ImageMap computes scale * R * uv + offset, where R (st * R in its row
  // vector convention) rotates by -angle. Decompose A = S * Rot(phi): the
  // rows of A are the scaled rows of the rotation.
  float sx = std::hypot(a00, a01);
  float sy = std::hypot(a10, a11);
  m_uvScale[0] = sx;
  m_uvScale[1] = sy;
  m_uvRotationDegrees = 0.f;
  if (sx > 0.f && sy > 0.f) {
    float r0x = a00 / sx, r0y = a01 / sx, r1x = a10 / sy, r1y = a11 / sy;
    if (a00 * a11 - a01 * a10 < 0.f) {
      // Mirrored: a negative scale on the second axis.
      m_uvScale[1] = -sy;
      r1x = -r1x;
      r1y = -r1y;
    }
    // Rot(phi) has the rows (cos, -sin) and (sin, cos).
    const float phi = std::atan2(r1x - r0y, r0x + r1y);
    m_uvRotationDegrees = -phi * (180.f / 3.14159265358979323846f);
    if (std::fabs(r0x * r1x + r0y * r1y) > 1e-3f) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "the image2D 'inTransform' has a non-uniform scale before a "
          "rotation (or a shear), which MoonRay's ImageMap can't express - "
          "the texture coordinates are transformed approximately");
    }
  }

  m_output.set(getParam<mat4>("outTransform", mat4(linalg::identity)),
      getParam<float4>("outOffset", float4(0.f)));
}

void Image2D::finalize()
{
  m_valid = false;

  // What the maps hold until they are synced again; see the header.
  for (int i = 0; i < 3; ++i) {
    m_previousTextures[i] = std::move(m_textures[i]);
    m_textures[i] = nullptr;
  }

  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'image' on the image2D sampler");
    return;
  }

  if (!texture::isImageType(m_image->elementType())) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image2D sampler cannot use an image of type %s as a texture",
        anari::toString(m_image->elementType()));
    return;
  }

  // attribute0 is the mesh's own st, which the map samples directly (with
  // texture filtering). The other slots reach the map through an AttributeMap
  // on its input coordinates.
  m_inputAttribute = nullptr;
  if (m_inAttribute != "attribute0") {
    if (m_inAttribute == "color"
        || !primitiveAttribute(
            m_inAttribute, m_inputAttribute, m_inputAttributeType)) {
      m_inputAttribute = nullptr;
      reportMessage(ANARI_SEVERITY_WARNING,
          "the image2D sampler reads 'attribute0' .. 'attribute3'; '%s' is "
          "not available",
          m_inAttribute.c_str());
      return;
    }
  }

  if (m_filter != "linear" && m_filter != "nearest") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'filter' \"%s\" on the image2D sampler - using linear",
        m_filter.c_str());
    m_filter = "linear";
  }

  for (const auto *mode : {&m_wrapMode1, &m_wrapMode2}) {
    if (*mode != "clampToEdge" && *mode != "repeat") {
      reportMessage(ANARI_SEVERITY_WARNING,
          "wrap mode \"%s\" is not supported (MoonRay's ImageMap has one "
          "repeat/clamp switch for both axes) - using clampToEdge",
          mode->c_str());
    }
  }
  if (m_wrapMode1 != m_wrapMode2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'wrapMode1' and 'wrapMode2' differ; MoonRay's ImageMap has a single "
        "repeat/clamp switch, so '%s' is used for both axes",
        m_wrapMode1.c_str());
  }

  m_valid = true;
}

namespace {

texture::ImageSource imageSource(const Array2D &image)
{
  texture::ImageSource source;
  source.array = &image;
  source.version = uint64_t(image.lastDataModified());
  source.width = image.size().x;
  source.height = image.size().y;
  source.type = image.elementType();
  source.data = image.data();
  return source;
}

} // namespace

texture::TexturePtr Image2D::plainTexture(std::string &error) const
{
  if (!m_image) {
    error = "the sampler has no 'image'";
    return nullptr;
  }
  return texture::acquireTexture(
      "image2d", imageSource(*m_image), texture::TextureLayout(), error);
}

bool Image2D::isValid() const
{
  return m_valid;
}

const char *Image2D::rdl2ClassName() const
{
  return "ImageMap";
}

const char *Image2D::inputPrimitiveAttribute() const
{
  return m_inputAttribute;
}

int Image2D::inputPrimitiveAttributeType() const
{
  return m_inputAttributeType;
}

void Image2D::syncRdl2(scene_rdl2::rdl2::SceneObject &obj, SamplerUsage usage)
{
  // MoonRay samples through OpenImageIO's texture system, which has no
  // nearest-neighbor mode reachable from ImageMap's parameters: "nearest" is
  // emulated by writing every texel as a block of pixels, so the filtering
  // only blurs the outermost pixel of each block (texture/Pixels.h).
  const uint32_t replicate = m_filter == "nearest"
      ? texture::nearestReplication(
          std::max(m_image->size().x, m_image->size().y))
      : 1u;

  texture::TextureLayout layout;
  layout.replicateX = replicate;
  layout.replicateY = replicate;

  std::string error;
  texture::TexturePtr texture = writeImageMapOutput(obj,
      "image2d",
      imageSource(*m_image),
      layout,
      planImageMapOutput(m_output, usage),
      error);
  if (!texture) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image2D sampler could not prepare its texture: %s",
        error.c_str());
  }
  // The previous texture of this usage (kept in m_previousTextures until
  // here if a commit came in between) is only let go now that the new one
  // exists - which, for unchanged pixels and transform, is the same one.
  m_textures[int(usage)] = std::move(texture);
  m_previousTextures[int(usage)] = nullptr;

  // 0 == "texture": sample with the mesh's st coordinates, which is what the
  // geometry's vertex.attribute0 becomes. 2 == "input texture coordinates":
  // the AttributeMap MoonrayScene binds for inputPrimitiveAttribute().
  obj.set("texture_coordinates", m_inputAttribute ? 2 : 0);
  obj.set("wrap_around", m_wrapMode1 == "repeat");
  obj.set("scale", scene_rdl2::rdl2::Vec2f(m_uvScale[0], m_uvScale[1]));
  obj.set("offset", scene_rdl2::rdl2::Vec2f(m_uvOffset[0], m_uvOffset[1]));
  obj.set("rotation_angle", m_uvRotationDegrees);
  obj.set("rotation_center", scene_rdl2::rdl2::Vec2f(0.f, 0.f));
}

} // namespace moonray_anari
