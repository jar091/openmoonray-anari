// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayDeviceGlobalState.h"

#include "backend/MoonrayScene.h"

namespace moonray_anari {

MoonrayDeviceGlobalState::MoonrayDeviceGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{}

// Out of line so the header can hold MoonrayScene by forward declaration.
MoonrayDeviceGlobalState::~MoonrayDeviceGlobalState() = default;

} // namespace moonray_anari
