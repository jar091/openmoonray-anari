// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace moonray_anari {

Instance::Instance(MoonrayDeviceGlobalState *s) : Object(ANARI_INSTANCE, s) {}

void Instance::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_xfm = getParam<mat4>("transform", mat4(linalg::identity));
  m_xfmInvRot = linalg::inverse(extractRotation(m_xfm));
  m_group = getParamObject<Group>("group");

  m_visibility = visibility::None;
  if (getParam<bool>("visibility.camera", true))
    m_visibility |= visibility::Camera;
  if (getParam<bool>("visibility.diffuse", true))
    m_visibility |= visibility::DiffuseReflection;
  if (getParam<bool>("visibility.glossy", true)) {
    m_visibility |=
        visibility::GlossyReflection | visibility::MirrorReflection;
  }
  if (getParam<bool>("visibility.transmission", true)) {
    m_visibility |= visibility::DiffuseTransmission
        | visibility::GlossyTransmission | visibility::MirrorTransmission;
  }
  if (getParam<bool>("visibility.volumeScatter", true))
    m_visibility |= visibility::Phase;
  if (getParam<bool>("visibility.shadow", true))
    m_visibility |= visibility::Shadow;

  m_objectColor = float4(0.f, 0.f, 0.f, 1.f);
  m_hasObjectColor = getParam("color", ANARI_FLOAT32_VEC4, &m_objectColor);
  if (!m_hasObjectColor) {
    float3 color(0.f);
    m_hasObjectColor = getParam("color", ANARI_FLOAT32_VEC3, &color);
    if (m_hasObjectColor)
      m_objectColor = float4(color.x, color.y, color.z, 1.f);
  }
  m_objectRandom = 0.f;
  m_hasObjectRandom = getParam("objectRandom", ANARI_FLOAT32, &m_objectRandom);
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");
}

uint32_t Instance::id() const
{
  return m_id;
}

const mat4 &Instance::xfm() const
{
  return m_xfm;
}

const mat3 &Instance::xfmInvRot() const
{
  return m_xfmInvRot;
}

bool Instance::xfmIsIdentity() const
{
  return xfm() == mat4(linalg::identity);
}

uint32_t Instance::visibility() const
{
  return m_visibility;
}

bool Instance::hasObjectColor() const
{
  return m_hasObjectColor;
}

const float4 &Instance::objectColor() const
{
  return m_objectColor;
}

bool Instance::hasObjectRandom() const
{
  return m_hasObjectRandom;
}

float Instance::objectRandom() const
{
  return m_objectRandom;
}

const Group *Instance::group() const
{
  return m_group.ptr;
}

Group *Instance::group()
{
  return m_group.ptr;
}

bool Instance::isValid() const
{
  return m_group;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Instance *);
