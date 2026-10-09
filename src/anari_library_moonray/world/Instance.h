// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

namespace moonray_anari {

// This basically implements just the 'transform' instance subtype. Use the
// pattern other objects employ for having multiple subtypes if your device
// wants to implement other instance subtypes.
struct Instance : public Object
{
  Instance(MoonrayDeviceGlobalState *s);
  ~Instance() override = default;

  void commitParameters() override;

  uint32_t id() const;

  const mat4 &xfm() const;
  const mat3 &xfmInvRot() const;
  bool xfmIsIdentity() const;

  const Group *group() const;
  Group *group();

  bool isValid() const override;

 private:
  uint32_t m_id{~0u};
  mat4 m_xfm;
  mat3 m_xfmInvRot;
  helium::IntrusivePtr<Group> m_group;
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Instance *, ANARI_INSTANCE);
