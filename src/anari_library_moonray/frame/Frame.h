// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "renderer/Renderer.h"
#include "world/World.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <future>
#include <vector>

namespace moonray_anari {

struct Frame : public helium::BaseFrame
{
  Frame(MoonrayDeviceGlobalState *s);
  ~Frame();

  bool isValid() const override;

  MoonrayDeviceGlobalState *deviceState() const;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  void renderFrame() override;

  void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void unmap(std::string_view channel) override;
  int frameReady(ANARIWaitMask m) override;
  void discard() override;

  bool ready() const;
  void wait() const;

 private:
  // Composite MoonRay's beauty buffer over the renderer's background and
  // convert to the requested color format.
  void writePixels();
  // Convert MoonRay's camera-space depth into ANARI's distance-along-the-ray.
  void writeDepth();
  // KHR_FRAME_ACCUMULATION: fold the render just made into the running
  // average (or restart it), leaving the average in m_renderBuffer.
  void accumulate(bool sceneChanged);

  //// Data ////

  bool m_valid{false};
  int m_perPixelBytes{4};
  uint2 m_size{0u, 0u};

  anari::DataType m_colorType{ANARI_UNKNOWN};
  anari::DataType m_depthType{ANARI_UNKNOWN};

  // MoonRay's output, straight from the render - or, when accumulating, the
  // average of the renders since the last change.
  std::vector<float4> m_renderBuffer;
  // KHR_FRAME_ACCUMULATION: the sample-weighted sum of the accumulated
  // renders and the total weight (samples per pixel) in it.
  bool m_accumulation{false};
  std::vector<float4> m_accumBuffer;
  float m_accumWeight{0.f};
  // MoonRay's depth output (camera-space z), and the distance-along-the-ray
  // the application sees.
  std::vector<float> m_renderDepth;
  std::vector<float> m_depthBuffer;
  // The application-visible channel.color buffer.
  std::vector<uint8_t> m_pixelBuffer;

  helium::IntrusivePtr<Renderer> m_renderer;
  helium::IntrusivePtr<Camera> m_camera;
  helium::IntrusivePtr<World> m_world;

  mutable std::future<void> m_future;
  std::string m_renderError;

  float m_duration{0.f};
};

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Frame *, ANARI_FRAME);
