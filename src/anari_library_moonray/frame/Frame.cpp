// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"

#include "backend/MoonrayScene.h"
#include "geometry/Geometry.h" // anariColorsToRgba
#include "light/HDRI.h"
#include "world/Group.h"
#include "world/Instance.h"
#include "world/World.h"
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace moonray_anari {

namespace {

uint8_t quantize(float v)
{
  v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
  // Round, do not truncate: truncation biases every channel down by up to a
  // full step (helium's cvt_color_to_uint32 does truncate).
  return static_cast<uint8_t>(v * 255.f + 0.5f);
}

// The exact sRGB transfer function. ANARI_UFIXED8_RGBA_SRGB names sRGB, so
// the piecewise curve is what the device writes - not the gamma-2.2
// approximation helium's helper uses, which is off by up to ~4/255 in the
// darks.
uint8_t quantizeSrgb(float linear)
{
  linear = linear < 0.f ? 0.f : (linear > 1.f ? 1.f : linear);
  const float encoded = linear <= 0.0031308f
      ? linear * 12.92f
      : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
  return quantize(encoded);
}

} // namespace

Frame::Frame(MoonrayDeviceGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  wait();
}

bool Frame::isValid() const
{
  return m_valid;
}

MoonrayDeviceGlobalState *Frame::deviceState() const
{
  return (MoonrayDeviceGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_world = getParamObject<World>("world");
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_size = getParam<uint2>("size", uint2(0u, 0u));
  m_accumulation = getParam<bool>("accumulation", false);

  if (m_depthType != ANARI_UNKNOWN && m_depthType != ANARI_FLOAT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'channel.depth' is only available as ANARI_FLOAT32");
    m_depthType = ANARI_UNKNOWN;
  }
}

void Frame::finalize()
{
  if (!m_renderer) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'renderer' on frame");
  }

  if (!m_camera) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'camera' on frame");
  }

  if (!m_world) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'world' on frame");
  }

  switch (m_colorType) {
  case ANARI_FLOAT32_VEC4:
    m_perPixelBytes = 16;
    break;
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_RGBA_SRGB:
    m_perPixelBytes = 4;
    break;
  default:
    m_perPixelBytes = 0;
    break;
  }

  const size_t numPixels = size_t(m_size.x) * size_t(m_size.y);
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);
  m_renderBuffer.assign(numPixels, float4(0.f, 0.f, 0.f, 0.f));
  // Committing the frame restarts the accumulation.
  m_accumBuffer.clear();
  m_accumWeight = 0.f;
  m_renderDepth.assign(m_depthType == ANARI_FLOAT32 ? numPixels : 0, 0.f);
  m_depthBuffer.assign(m_depthType == ANARI_FLOAT32 ? numPixels : 0, 0.f);

  m_valid = m_renderer && m_renderer->isValid() && m_camera
      && m_camera->isValid() && m_world && m_world->isValid()
      && m_perPixelBytes > 0 && numPixels > 0;
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    if (flags & ANARI_WAIT)
      wait();
    helium::writeToVoidP(ptr, m_duration);
    return true;
  }

  return 0;
}

void Frame::renderFrame()
{
  wait();

  auto *state = deviceState();
  state->commitBuffer.flush();

  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "skipping render of an incomplete frame (see the warnings above)");
    return;
  }

  auto *scene = state->scene.get();
  if (!scene || !scene->valid()) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "MoonRay is not available: %s",
        scene ? scene->error().c_str() : "no renderer backend");
    return;
  }

  const auto start = std::chrono::steady_clock::now();

  m_future = std::async(std::launch::async, [this, scene, start]() {
    m_renderError.clear();
    bool sceneChanged = true;
    if (!scene->render(m_world.ptr,
            m_camera.ptr,
            m_renderer.ptr,
            m_size,
            m_renderBuffer,
            m_depthType == ANARI_FLOAT32 ? &m_renderDepth : nullptr,
            m_accumulation,
            sceneChanged,
            m_renderError)) {
      reportMessage(
          ANARI_SEVERITY_ERROR, "MoonRay render failed: %s",
          m_renderError.c_str());
      m_accumWeight = 0.f;
    } else {
      accumulate(sceneChanged);
    }
    writePixels();
    writeDepth();
    const auto end = std::chrono::steady_clock::now();
    m_duration = std::chrono::duration<float>(end - start).count();
  });
}

void Frame::accumulate(bool sceneChanged)
{
  if (!m_accumulation || !m_renderer)
    return;

  // Weighted by samples per pixel, so a pixelSamples change in the middle of
  // an accumulation still averages correctly.
  const int n = m_renderer->pixelSamplesSqrt();
  const float weight = float(n * n);
  if (sceneChanged || m_accumWeight <= 0.f
      || m_accumBuffer.size() != m_renderBuffer.size()) {
    m_accumBuffer.assign(m_renderBuffer.size(), float4(0.f, 0.f, 0.f, 0.f));
    m_accumWeight = 0.f;
  }

  m_accumWeight += weight;
  const float inv = 1.f / m_accumWeight;
  for (size_t i = 0; i < m_renderBuffer.size(); ++i) {
    m_accumBuffer[i] += weight * m_renderBuffer[i];
    m_renderBuffer[i] = m_accumBuffer[i] * inv;
  }
}

void Frame::writePixels()
{
  const float4 bgColor =
      m_renderer ? m_renderer->background() : float4(0.f, 0.f, 0.f, 1.f);

  // A visible hdri light is the background itself: MoonRay renders it with
  // zero alpha (lights don't cover pixels), so it must not get the
  // background color composited under it as well.
  bool environmentBackground = false;
  if (m_world) {
    for (const Instance *instance : m_world->instances()) {
      if (!instance || !instance->group())
        continue;
      for (const Light *light : instance->group()->lights()) {
        const auto *hdri = dynamic_cast<const HDRI *>(light);
        if (hdri && hdri->isValid() && hdri->visible())
          environmentBackground = true;
      }
    }
  }

  // A background image is stretched over the whole frame and filtered
  // bilinearly with clamped edges - the texel centers of a 1x2 image sit at
  // a quarter and three quarters of the height, and the gradient between
  // them is linear. Its first row is the bottom of the frame, like the
  // frame's own first row.
  std::vector<float> bgImage;
  uint2 bgSize(0u, 0u);
  const Array2D *image = m_renderer ? m_renderer->backgroundImage() : nullptr;
  if (image) {
    if (anariColorsToRgba(
            image->elementType(), image->data(), image->totalSize(), bgImage))
      bgSize = uint2(image->size().x, image->size().y);
  }
  auto backgroundAt = [&](uint32_t x, uint32_t y) -> float4 {
    if (bgSize.x == 0 || bgSize.y == 0)
      return bgColor;
    auto texel = [&](int tx, int ty) {
      tx = std::clamp(tx, 0, int(bgSize.x) - 1);
      ty = std::clamp(ty, 0, int(bgSize.y) - 1);
      const float *p = bgImage.data() + 4 * (size_t(ty) * bgSize.x + tx);
      return float4(p[0], p[1], p[2], p[3]);
    };
    const float fx =
        (float(x) + 0.5f) / float(m_size.x) * float(bgSize.x) - 0.5f;
    const float fy =
        (float(y) + 0.5f) / float(m_size.y) * float(bgSize.y) - 0.5f;
    const int x0 = int(std::floor(fx));
    const int y0 = int(std::floor(fy));
    const float ax = fx - float(x0);
    const float ay = fy - float(y0);
    const float4 bottom =
        texel(x0, y0) * (1.f - ax) + texel(x0 + 1, y0) * ax;
    const float4 top =
        texel(x0, y0 + 1) * (1.f - ax) + texel(x0 + 1, y0 + 1) * ax;
    return bottom * (1.f - ay) + top * ay;
  };

  const size_t numPixels = size_t(m_size.x) * size_t(m_size.y);
  for (size_t i = 0; i < numPixels && i < m_renderBuffer.size(); ++i) {
    // MoonRay's beauty buffer carries associated (premultiplied) alpha, so
    // the background goes *under* it with the standard 'over' operator.
    const float4 src = m_renderBuffer[i];
    const float t = 1.f - src.w;
    const float4 bg = environmentBackground || src.w >= 1.f
        ? float4(0.f, 0.f, 0.f, 0.f)
        : backgroundAt(uint32_t(i % m_size.x), uint32_t(i / m_size.x));
    const float4 c = environmentBackground
        ? float4(src.x, src.y, src.z, 1.f)
        : float4(src.x + t * bg.x,
              src.y + t * bg.y,
              src.z + t * bg.z,
              src.w + t * bg.w);

    auto *dst = m_pixelBuffer.data() + (i * m_perPixelBytes);
    switch (m_colorType) {
    case ANARI_UFIXED8_VEC4: {
      dst[0] = quantize(c.x);
      dst[1] = quantize(c.y);
      dst[2] = quantize(c.z);
      dst[3] = quantize(c.w);
      break;
    }
    case ANARI_UFIXED8_RGBA_SRGB: {
      // Alpha stays linear - only the color channels are sRGB encoded.
      dst[0] = quantizeSrgb(c.x);
      dst[1] = quantizeSrgb(c.y);
      dst[2] = quantizeSrgb(c.z);
      dst[3] = quantize(c.w);
      break;
    }
    case ANARI_FLOAT32_VEC4: {
      std::memcpy(dst, &c, sizeof(c));
      break;
    }
    default:
      break;
    }
  }
}

void Frame::writeDepth()
{
  if (m_depthBuffer.empty() || m_renderDepth.size() != m_depthBuffer.size())
    return;

  // MoonRay's depth AOV is the camera-space z distance (Aov.cc: "state depth
  // (-1.f * P.z)"), while ANARI's channel.depth is the distance from the
  // camera to the hit. For a perspective camera those differ by exactly the
  // ray's inclination, which the camera's image-plane extent gives:
  //
  //     distance = z * sqrt(1 + u^2 + v^2)
  //
  // with (u,v) the camera-space image-plane coordinates of the pixel. A
  // camera with parallel rays reports (0,0) and the two are the same.
  const float imageAspect = m_size.y > 0
      ? float(m_size.x) / float(m_size.y)
      : 1.f;
  const float2 extent =
      m_camera ? m_camera->imagePlaneHalfExtent(imageAspect) : float2(0.f, 0.f);

  for (uint32_t y = 0; y < m_size.y; ++y) {
    const float v =
        (2.f * (float(y) + 0.5f) / float(m_size.y) - 1.f) * extent.y;
    for (uint32_t x = 0; x < m_size.x; ++x) {
      const float u =
          (2.f * (float(x) + 0.5f) / float(m_size.x) - 1.f) * extent.x;
      const size_t i = size_t(y) * m_size.x + x;
      const float z = m_renderDepth[i];
      // MoonRay leaves the depth output at zero where nothing was hit; ANARI
      // applications expect "no hit" to be infinitely far away, not at the
      // camera.
      m_depthBuffer[i] = z > 0.f
          ? z * std::sqrt(1.f + u * u + v * v)
          : std::numeric_limits<float>::infinity();
    }
  }
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  wait();

  *width = m_size.x;
  *height = m_size.y;

  if (channel == "channel.color" && !m_pixelBuffer.empty()) {
    *pixelType = m_colorType;
    return m_pixelBuffer.data();
  }

  if (channel == "channel.depth" && !m_depthBuffer.empty()) {
    *pixelType = ANARI_FLOAT32;
    return m_depthBuffer.data();
  }

  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;
  return nullptr;
}

void Frame::unmap(std::string_view channel)
{
  // The mapped buffers are device-owned and read-only to the application;
  // nothing has to happen on unmap.
}

int Frame::frameReady(ANARIWaitMask m)
{
  if (m == ANARI_NO_WAIT)
    return ready();

  wait();
  return 1;
}

void Frame::discard()
{
  auto *state = deviceState();
  if (state->scene)
    state->scene->requestStop();
  wait();
}

bool Frame::ready() const
{
  if (!m_future.valid())
    return true;
  return m_future.wait_for(std::chrono::seconds(0))
      == std::future_status::ready;
}

void Frame::wait() const
{
  if (m_future.valid())
    m_future.get();
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Frame *);
