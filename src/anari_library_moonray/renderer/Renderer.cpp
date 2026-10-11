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
  m_maxMeshLights = hasParam("moonray.maxMeshLights")
      ? std::max(0, getParam<int>("moonray.maxMeshLights", 64))
      : -1;
  m_maxMeshLightTriangles =
      std::max(0, getParam<int>("moonray.maxMeshLightTriangles", 500000));
  m_minMeshLightPowerShare = std::min(1.f,
      std::max(0.f, getParam<float>("moonray.minMeshLightPowerShare", 0.02f)));

  // --- path depths ---------------------------------------------------------
  // The parameters count bounces the way Cycles does - maxDiffuseBounce = 2
  // is two diffuse bounces after the camera hit - and so does MoonRay: a hit
  // spawns an indirect ray of a lobe type while the path's count of that type
  // is *below* the limit (PathIntegrator: pv.diffuseDepth <
  // mMaxDiffuseDepth). The numbers therefore go across unchanged, except for
  // volumes, where MoonRay's limit counts scattering orders (1 = single
  // scattering) and the parameter counts the bounces after the first.
  //
  // What does not go across one to one is the split: MoonRay limits diffuse,
  // glossy and mirror lobes, each for reflection and transmission together,
  // where the parameters have diffuse and glossy *reflection* and one limit
  // for all transmission. So
  //   - diffuse and glossy map to their lobe types (and with them also limit
  //     diffuse and glossy transmission);
  //   - the mirror limit covers what both a glossy reflection off a perfect
  //     mirror and a refraction through clear glass are in MoonRay, and gets
  //     the larger of maxGlossyBounce and maxTransmissionBounce, so that
  //     neither is cut short.
  // The defaults are MoonRay's own.
  const int maxBounce = std::max(0, getParam<int>("maxBounce", 5));
  const int maxDiffuse = std::max(0, getParam<int>("maxDiffuseBounce", 2));
  const int maxGlossy = std::max(0, getParam<int>("maxGlossyBounce", 2));
  const int maxTransmission =
      std::max(0, getParam<int>("maxTransmissionBounce", 3));
  const int maxVolume = std::max(0, getParam<int>("maxVolumeBounce", 0));
  const int maxTransparency =
      std::max(0, getParam<int>("maxTransparencyBounce", 16));
  m_integrator.maxDepth = maxBounce;
  m_integrator.maxDiffuseDepth = maxDiffuse;
  m_integrator.maxGlossyDepth = maxGlossy;
  m_integrator.maxMirrorDepth = std::max(maxGlossy, maxTransmission);
  m_integrator.maxVolumeDepth = maxVolume + 1;
  // How many cutout (presence) surfaces in a row a path passes through;
  // beyond that MoonRay takes the surface as fully present.
  m_integrator.maxPresenceDepth = maxTransparency;

  // --- sample clamping -------------------------------------------------------
  // MoonRay has one limit and a depth from which it applies: the tentative
  // contribution of every direct lighting sample (a light sample, or a BSDF
  // sample that reaches a light) at a hit with at least
  // sample_clamping_depth non-mirror bounces before it is scaled down until
  // its largest component is the limit (PathIntegratorUtil: smartClamp).
  // clampIndirect is that limit from the first bounce on (depth 1); the
  // camera hit is included (depth 0) when clampDirect asks for a limit that
  // is not looser. A direct limit that is looser than the indirect one, or
  // one without an indirect limit, cannot be expressed and leaves the
  // direct light unclamped. 0 means no clamping, as in Cycles.
  const float clampDirect = std::max(0.f, getParam<float>("clampDirect", 0.f));
  const float clampIndirect =
      std::max(0.f, getParam<float>("clampIndirect", 10.f));
  m_integrator.sampleClampingValue = clampIndirect;
  m_integrator.sampleClampingDepth =
      clampIndirect > 0.f && clampDirect > 0.f && clampDirect <= clampIndirect
          ? 0
          : 1;
  if (clampDirect > 0.f && clampIndirect == 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'clampDirect' without 'clampIndirect' has no effect: MoonRay has a "
        "single sample clamping value, which cannot limit the direct light "
        "alone");
  }

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

int Renderer::maxMeshLights() const
{
  return m_maxMeshLights;
}

int Renderer::maxMeshLightTriangles() const
{
  return m_maxMeshLightTriangles;
}

float Renderer::minMeshLightPowerShare() const
{
  return m_minMeshLightPowerShare;
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

const Renderer::Integrator &Renderer::integrator() const
{
  return m_integrator;
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Renderer *);
