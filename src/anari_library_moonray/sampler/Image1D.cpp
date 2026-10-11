// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image1D.h"
#include "geometry/Geometry.h" // Geometry::attribute0Name

// anari
#include <anari/frontend/type_utility.h>
// scene_rdl2
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Types.h>

namespace moonray_anari {

Image1D::Image1D(MoonrayDeviceGlobalState *s) : Sampler(s), m_image(this) {}

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

  m_output.set(getParam<mat4>("outTransform", mat4(linalg::identity)),
      getParam<float4>("outOffset", float4(0.f)));
}

void Image1D::finalize()
{
  m_valid = false;

  for (int i = 0; i < 3; ++i) {
    m_previousTextures[i] = std::move(m_textures[i]);
    m_textures[i] = nullptr;
  }

  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'image' on the image1D sampler");
    return;
  }

  if (!texture::isImageType(m_image->elementType())) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image1D sampler cannot use an image of type %s as a texture",
        anari::toString(m_image->elementType()));
    return;
  }

  // attribute0 is exported by every geometry that has it, as a vec2f; the
  // other slots are the vec3f attributes of the triangle geometry.
  if (m_inAttribute == "attribute0") {
    m_inputAttribute = Geometry::attribute0Name;
    m_inputAttributeType = 1;
  } else if (m_inAttribute == "color"
      || !primitiveAttribute(
          m_inAttribute, m_inputAttribute, m_inputAttributeType)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image1D sampler reads 'attribute0' .. 'attribute3'; '%s' is not "
        "available",
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

  m_valid = true;
}

bool Image1D::isValid() const
{
  return m_valid;
}

const char *Image1D::rdl2ClassName() const
{
  return "ImageMap";
}

void Image1D::syncRdl2(scene_rdl2::rdl2::SceneObject &obj, SamplerUsage usage)
{
  texture::ImageSource source;
  source.array = m_image.get();
  source.version = uint64_t(m_image->lastDataModified());
  source.width = m_image->size();
  source.height = 1;
  source.type = m_image->elementType();
  source.data = m_image->data();

  // filter = "nearest" is emulated as for image2D: every texel becomes a run
  // of identical pixels, so the texture system's filtering only blurs the
  // outermost pixel of each run.
  texture::TextureLayout layout;
  layout.replicateX = m_filter == "nearest"
      ? texture::nearestReplication(m_image->size())
      : 1u;
  layout.replicateY = 1;

  std::string error;
  texture::TexturePtr texture = writeImageMapOutput(obj,
      "image1d",
      source,
      layout,
      planImageMapOutput(m_output, usage),
      error);
  if (!texture) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "the image1D sampler could not prepare its texture: %s",
        error.c_str());
  }
  m_textures[int(usage)] = std::move(texture);
  m_previousTextures[int(usage)] = nullptr;

  // 2 == "input texture coordinates": st comes from the AttributeMap bound
  // to input_texture_coordinates (inputPrimitiveAttribute()). Its t is
  // irrelevant - the texture is a single row.
  obj.set("texture_coordinates", 2);
  obj.set("wrap_around", m_wrapMode == "repeat");
  obj.set("scale", scene_rdl2::rdl2::Vec2f(m_uScale, 1.f));
  obj.set("offset", scene_rdl2::rdl2::Vec2f(m_uOffset, 0.f));
  obj.set("rotation_angle", 0.f);
}

const char *Image1D::inputPrimitiveAttribute() const
{
  return m_inputAttribute;
}

int Image1D::inputPrimitiveAttributeType() const
{
  return m_inputAttributeType;
}

} // namespace moonray_anari
