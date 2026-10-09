// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
#include "TransferFunction1D.h"

namespace moonray_anari {

Volume::Volume(MoonrayDeviceGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume *Volume::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  // unknown subtype: isValid() == false
  auto *volume = new Volume(s);
  reportUnknownSubtype(volume, "volume", subtype);
  return volume;
}

void Volume::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
}

bool Volume::isValid() const
{
  return false;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Volume *);
