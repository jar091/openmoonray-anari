// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array2D.h"

namespace moonray_anari {

// ANARI renderer. Only the 'default' subtype exists; it drives MoonRay's
// batch CPU renderer.
struct Renderer : public Object
{
  Renderer(MoonrayDeviceGlobalState *s);
  ~Renderer() override = default;

  static Renderer *createInstance(
      std::string_view subtype, MoonrayDeviceGlobalState *d);

  void commitParameters() override;

  bool isValid() const override;

  // Background color and alpha, composited under the rendered image.
  float4 background() const;
  // The background image (KHR_RENDERER_BACKGROUND_IMAGE), or nullptr when the
  // background is a color. It is stretched over the whole frame and
  // composited the same way as the color.
  const Array2D *backgroundImage() const;

  // MoonRay's per-hit sample splitting: light_samples and bsdf_samples (both
  // per-axis counts, so N means N*N samples). See commitParameters().
  int lightSamples() const;
  int bsdfSamples() const;

  // How many MeshLight objects the device creates at most for emitting
  // surfaces (`moonray.maxMeshLights`): MoonRay samples every light at every
  // hit, so each one costs a shadow ray per sample. Negative when the
  // renderer does not say: the parameter of the same name on the device
  // counts then.
  int maxMeshLights() const;
  // How many triangles the meshes of those lights may have together
  // (`moonray.maxMeshLightTriangles`): MoonRay builds the sampling structure
  // of a mesh light again for every edit of the light, its material or its
  // object, in about a second per 250 000 triangles.
  int maxMeshLightTriangles() const;
  // The share of the power of all emitters which ask for lights below which
  // an emitter gets none (`moonray.minMeshLightPowerShare`): every light
  // costs a shadow ray at every hit, and a lamp which adds a hundredth of the
  // light adds a hundredth of the noise as a surface.
  float minMeshLightPowerShare() const;

  // Requested samples per pixel. MoonRay samples a square grid, so the
  // effective count is pixelSamplesSqrt()^2.
  int pixelSamples() const;
  int pixelSamplesSqrt() const;

  // Uniform environment light (MoonRay EnvLight) added to the scene when
  // ambientRadiance() > 0.
  float3 ambientColor() const;
  float ambientRadiance() const;

  // MoonRay's path depth limits and sample clamping (rdl2::SceneVariables),
  // from the renderer parameters maxBounce, maxDiffuseBounce,
  // maxGlossyBounce, maxTransmissionBounce, maxVolumeBounce,
  // maxTransparencyBounce, clampDirect and clampIndirect - see
  // commitParameters() for the mapping.
  struct Integrator
  {
    int maxDepth{5};
    int maxDiffuseDepth{2};
    int maxGlossyDepth{2};
    int maxMirrorDepth{3};
    int maxVolumeDepth{1};
    int maxPresenceDepth{16};
    float sampleClampingValue{10.f};
    int sampleClampingDepth{1};

    bool operator==(const Integrator &o) const
    {
      return maxDepth == o.maxDepth && maxDiffuseDepth == o.maxDiffuseDepth
          && maxGlossyDepth == o.maxGlossyDepth
          && maxMirrorDepth == o.maxMirrorDepth
          && maxVolumeDepth == o.maxVolumeDepth
          && maxPresenceDepth == o.maxPresenceDepth
          && sampleClampingValue == o.sampleClampingValue
          && sampleClampingDepth == o.sampleClampingDepth;
    }
  };
  const Integrator &integrator() const;

 private:
  float4 m_background{0.f, 0.f, 0.f, 1.f};
  helium::ChangeObserverPtr<Array2D> m_backgroundImage;
  int m_lightSamples{1};
  int m_bsdfSamples{1};
  int m_maxMeshLights{-1};
  int m_maxMeshLightTriangles{500000};
  float m_minMeshLightPowerShare{0.02f};
  int m_pixelSamples{16};
  float3 m_ambientColor{1.f, 1.f, 1.f};
  float m_ambientRadiance{0.f};
  Integrator m_integrator;
  bool m_valid{true};
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Renderer *, ANARI_RENDERER);
