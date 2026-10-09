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

  // Requested samples per pixel. MoonRay samples a square grid, so the
  // effective count is pixelSamplesSqrt()^2.
  int pixelSamples() const;
  int pixelSamplesSqrt() const;

  // Uniform environment light (MoonRay EnvLight) added to the scene when
  // ambientRadiance() > 0.
  float3 ambientColor() const;
  float ambientRadiance() const;

 private:
  float4 m_background{0.f, 0.f, 0.f, 1.f};
  helium::ChangeObserverPtr<Array2D> m_backgroundImage;
  int m_lightSamples{1};
  int m_bsdfSamples{1};
  int m_pixelSamples{16};
  float3 m_ambientColor{1.f, 1.f, 1.f};
  float m_ambientRadiance{0.f};
  bool m_valid{true};
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Renderer *, ANARI_RENDERER);
