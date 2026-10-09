// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
#include "geometry/Geometry.h" // anariColorsToRgba
// std
#include <algorithm>
#include <cmath>
#include <vector>

namespace moonray_anari {

Renderer::Renderer(MoonrayDeviceGlobalState *s)
    : Object(ANARI_RENDERER, s), m_backgroundImage(this)
{}

Renderer *Renderer::createInstance(
    std::string_view subtype, MoonrayDeviceGlobalState *s)
{
  auto *r = new Renderer(s);
  if (!subtype.empty() && subtype != "default") {
    r->reportMessage(ANARI_SEVERITY_WARNING,
        "unknown renderer subtype - only 'default' is implemented");
    r->m_valid = false;
  }
  return r;
}

void Renderer::commitParameters()
{
  // 'background' is either a color or an image (an ARRAY2D of colors).
  m_backgroundImage = getParamObject<Array2D>("background");
  m_background = getParam<float4>("background", float4(0.f, 0.f, 0.f, 1.f));
  if (m_backgroundImage) {
    std::vector<float> probe;
    if (m_backgroundImage->totalSize() == 0
        || !anariColorsToRgba(m_backgroundImage->elementType(),
            m_backgroundImage->data(),
            1,
            probe)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "the 'background' image has an element type this device cannot "
          "read (%s) or is empty - using a black background",
          anari::toString(m_backgroundImage->elementType()));
      m_backgroundImage = nullptr;
      m_background = float4(0.f, 0.f, 0.f, 1.f);
    }
  }
  m_pixelSamples = getParam<int>("pixelSamples", 16);
  m_ambientColor = getParam<float3>("ambientColor", float3(1.f, 1.f, 1.f));
  m_ambientRadiance = getParam<float>("ambientRadiance", 0.f);

  // MoonRay splits every camera sample at its first hit into light_samples^2
  // samples per light and bsdf_samples^2 samples per BSDF lobe; its defaults
  // (2, i.e. four of each) multiply the direct-lighting work of every pixel
  // sample about fourfold. ANARI's 'pixelSamples' is the number of samples
  // (paths) per pixel the application asked for, which is what other devices
  // trace, so the device asks for one of each: the same converged image at
  // roughly half the cost. The vendor parameters restore MoonRay's splitting
  // for applications that want it.
  m_lightSamples = std::max(1, getParam<int>("moonray.lightSamples", 1));
  m_bsdfSamples = std::max(1, getParam<int>("moonray.bsdfSamples", 1));

  if (m_pixelSamples < 1) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'pixelSamples' must be at least 1, got %i - using 1",
        m_pixelSamples);
    m_pixelSamples = 1;
  }
}

bool Renderer::isValid() const
{
  return m_valid;
}

float4 Renderer::background() const
{
  return m_background;
}

const Array2D *Renderer::backgroundImage() const
{
  return m_backgroundImage.get();
}

int Renderer::lightSamples() const
{
  return m_lightSamples;
}

int Renderer::bsdfSamples() const
{
  return m_bsdfSamples;
}

int Renderer::pixelSamples() const
{
  return m_pixelSamples;
}

int Renderer::pixelSamplesSqrt() const
{
  // MoonRay samples an NxN grid per pixel, so a request that is not a perfect
  // square is rounded *up* to the next one - never silently down.
  return static_cast<int>(
      std::ceil(std::sqrt(static_cast<double>(m_pixelSamples))));
}

float3 Renderer::ambientColor() const
{
  return m_ambientColor;
}

float Renderer::ambientRadiance() const
{
  return m_ambientRadiance;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Renderer *);
