// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace moonray_anari {

// Base of all ANARI volumes. An instance of *this* class is what an unknown
// volume subtype produces: isValid() == false, so it never reaches MoonRay.
struct Volume : public Object
{
  Volume(MoonrayDeviceGlobalState *d);
  virtual ~Volume() = default;
  static Volume *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *d);

  void commitParameters() override;

  bool isValid() const override;

  uint32_t id() const;

 private:
  uint32_t m_id{~0u};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Volume::id() const
{
  return m_id;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Volume *, ANARI_VOLUME);
