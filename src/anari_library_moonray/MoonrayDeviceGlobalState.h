// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// std
#include <memory>

namespace moonray_anari {

struct MoonrayScene;

struct MoonrayDeviceGlobalState : public helium::BaseGlobalDeviceState
{
  // The device's MoonRay renderer and its RDL2 scene. Created by
  // MoonrayDevice::initDevice() on first object creation, so simply loading
  // the library (anariLoadLibrary + introspection) never starts a renderer.
  std::unique_ptr<MoonrayScene> scene;

  MoonrayDeviceGlobalState(ANARIDevice d);
  ~MoonrayDeviceGlobalState();
};

// Helper functions/macros ////////////////////////////////////////////////////

inline MoonrayDeviceGlobalState *asMoonrayDeviceState(
    helium::BaseGlobalDeviceState *s)
{
  return (MoonrayDeviceGlobalState *)s;
}

#define Moonray_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define Moonray_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace moonray_anari
