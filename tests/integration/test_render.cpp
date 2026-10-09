// Milestone 7 ANARI render tests: the first end-to-end MoonRay render driven
// entirely through the public ANARI C API (anariLoadLibrary — no internal
// adapter class is linked, master prompt §31.2).
//
// The scene is the milestone's required one: perspective camera, triangle
// geometry, matte material, surface, world, one light, default renderer,
// frame. Every expectation below is *derived* rather than eyeballed:
//
//   A Lambertian surface of albedo a, facing a directional light of
//   irradiance E head-on, has outgoing radiance L = a * E / pi. The tests
//   therefore use E = pi, which makes the lit triangle's color exactly the
//   material color, and check the background where no geometry is hit.
//
// Usage: integration_render <triangle_matte|frame_color_float|frame_depth>

#include "TriangleScene.h"

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK_MSG(cond, msg)                                                   \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, msg);    \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#define CHECK(cond) CHECK_MSG(cond, "")

using test_scene::kPi;
constexpr uint32_t kSize = 64;

void statusFunc(const void * /*userData*/,
    ANARIDevice /*device*/,
    ANARIObject /*source*/,
    ANARIDataType /*sourceType*/,
    ANARIStatusSeverity severity,
    ANARIStatusCode /*code*/,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR || severity == ANARI_SEVERITY_ERROR) {
    std::printf("[anari][error] %s\n", message);
    ++failures;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::printf("[anari][warn ] %s\n", message);
  }
}

bool close(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

// --- anari.triangle_matte ---------------------------------------------------

int testTriangleMatte(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};

  auto s = test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);

  anariRenderFrame(d, s.frame);
  anariFrameReady(d, s.frame, ANARI_WAIT);

  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  const auto *pixels = (const float *)anariMapFrame(
      d, s.frame, "channel.color", &width, &height, &pixelType);

  CHECK_MSG(pixels != nullptr, "channel.color could not be mapped");
  CHECK(width == kSize);
  CHECK(height == kSize);
  CHECK(pixelType == ANARI_FLOAT32_VEC4);

  if (pixels) {
    bool allFinite = true;
    unsigned litPixels = 0;
    for (uint32_t i = 0; i < width * height; ++i) {
      for (int c = 0; c < 4; ++c) {
        if (!std::isfinite(pixels[4 * i + c]))
          allFinite = false;
      }
      // Anything that is not the pure blue background counts as covered.
      if (pixels[4 * i + 0] > 0.01f)
        ++litPixels;
    }
    CHECK_MSG(allFinite, "frame contains non-finite values");

    // The triangle covers a quarter of the frame or so; require a real
    // fraction so a black or empty frame cannot pass.
    CHECK_MSG(litPixels > (width * height) / 10,
        "the triangle covers almost none of the frame");

    // Center of the frame is inside the triangle: L = albedo * E / pi with
    // E = pi, so the color is the albedo itself and alpha is 1.
    const uint32_t center = (height / 2) * width + (width / 2);
    const float *c = pixels + 4 * center;
    std::printf("center pixel: %.4f %.4f %.4f %.4f (expected %.4f %.4f %.4f 1)\n",
        c[0], c[1], c[2], c[3], albedo[0], albedo[1], albedo[2]);
    CHECK_MSG(close(c[0], albedo[0], 0.05f), "center pixel red channel");
    CHECK_MSG(close(c[1], albedo[1], 0.05f), "center pixel green channel");
    CHECK_MSG(close(c[2], albedo[2], 0.05f), "center pixel blue channel");
    CHECK_MSG(close(c[3], 1.f, 0.02f), "center pixel alpha");

    // Top-left corner is outside the triangle: pure background.
    const uint32_t corner = (height - 1) * width;
    const float *b = pixels + 4 * corner;
    std::printf("corner pixel: %.4f %.4f %.4f %.4f (expected %.4f %.4f %.4f %.4f)\n",
        b[0], b[1], b[2], b[3], background[0], background[1], background[2],
        background[3]);
    CHECK_MSG(close(b[0], background[0], 0.01f), "corner pixel red channel");
    CHECK_MSG(close(b[1], background[1], 0.01f), "corner pixel green channel");
    CHECK_MSG(close(b[2], background[2], 0.01f), "corner pixel blue channel");
    CHECK_MSG(close(b[3], background[3], 0.01f), "corner pixel alpha");
  }

  anariUnmapFrame(d, s.frame, "channel.color");

  float duration = -1.f;
  anariGetProperty(
      d, s.frame, "duration", ANARI_FLOAT32, &duration, sizeof(duration), ANARI_WAIT);
  CHECK_MSG(duration > 0.f, "frame duration property was not reported");

  test_scene::release(s);
  return failures;
}

// --- anari.frame_color_float ------------------------------------------------
//
// The float channel is the reference; the fixed-point channels have to be the
// same image, quantized. That is what makes this a test of the *conversion*
// rather than of the renderer.

float srgbEncode(float linear)
{
  linear = linear < 0.f ? 0.f : (linear > 1.f ? 1.f : linear);
  return linear <= 0.0031308f ? linear * 12.92f
                              : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
}

int testFrameColorFloat(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};

  std::vector<float> reference;

  {
    auto s = test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);
    anariRenderFrame(d, s.frame);
    anariFrameReady(d, s.frame, ANARI_WAIT);

    uint32_t w = 0, h = 0;
    ANARIDataType t = ANARI_UNKNOWN;
    const auto *pixels =
        (const float *)anariMapFrame(d, s.frame, "channel.color", &w, &h, &t);
    CHECK_MSG(pixels != nullptr, "float channel.color could not be mapped");
    CHECK(t == ANARI_FLOAT32_VEC4);
    if (pixels)
      reference.assign(pixels, pixels + size_t(w) * h * 4);
    anariUnmapFrame(d, s.frame, "channel.color");
    test_scene::release(s);
  }

  {
    auto s = test_scene::build(d, albedo, background, ANARI_UFIXED8_RGBA_SRGB, kSize);
    anariRenderFrame(d, s.frame);
    anariFrameReady(d, s.frame, ANARI_WAIT);

    uint32_t w = 0, h = 0;
    ANARIDataType t = ANARI_UNKNOWN;
    const auto *pixels = (const uint8_t *)anariMapFrame(
        d, s.frame, "channel.color", &w, &h, &t);
    CHECK_MSG(pixels != nullptr, "srgb channel.color could not be mapped");
    CHECK(t == ANARI_UFIXED8_RGBA_SRGB);

    if (pixels && reference.size() == size_t(w) * h * 4) {
      float worst = 0.f;
      for (size_t i = 0; i < size_t(w) * h; ++i) {
        for (int c = 0; c < 3; ++c) {
          const float expected = srgbEncode(reference[4 * i + c]) * 255.f;
          const float actual = float(pixels[4 * i + c]);
          worst = std::fmax(worst, std::fabs(expected - actual));
        }
      }
      std::printf("worst sRGB quantization difference: %.2f/255\n", worst);
      // One quantization step of slack; anything larger means the conversion
      // (or the render) differs, not the rounding.
      CHECK_MSG(worst <= 1.5f, "sRGB channel does not match the float channel");
    }

    anariUnmapFrame(d, s.frame, "channel.color");
    test_scene::release(s);
  }

  return failures;
}

// --- anari.frame_depth ------------------------------------------------------
//
// MoonRay reports a camera-space z distance; ANARI's channel.depth is the
// distance from the camera to the hit. The test scene makes the difference
// measurable: the triangle lies in the z = 0 plane with the camera 3 units in
// front of it, so *every* hit is at z = 3 but the distance grows towards the
// edges of the frame by exactly 1/cos(angle).

int testFrameDepth(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};

  auto s = test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);

  const auto depthType = ANARI_FLOAT32;
  anariSetParameter(d, s.frame, "channel.depth", ANARI_DATA_TYPE, &depthType);
  anariCommitParameters(d, s.frame);

  anariRenderFrame(d, s.frame);
  anariFrameReady(d, s.frame, ANARI_WAIT);

  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *depth =
      (const float *)anariMapFrame(d, s.frame, "channel.depth", &w, &h, &t);

  CHECK_MSG(depth != nullptr, "channel.depth could not be mapped");
  CHECK(t == ANARI_FLOAT32);
  CHECK(w == kSize && h == kSize);

  if (depth) {
    // The center of the frame looks straight down the view axis, so there the
    // distance is the plane distance itself.
    const float center = depth[(kSize / 2) * kSize + kSize / 2];
    std::printf("center depth %.4f (expected 3.0)\n", center);
    CHECK_MSG(close(center, 3.f, 0.05f),
        "the depth at the center of the frame is not the distance to the plane");

    // A pixel away from the axis sees the same plane at an angle, so its
    // distance is larger by exactly 1/cos: with
    //   u = (2*(x+0.5)/W - 1) * tan(fovy/2) * aspect
    //   v = (2*(y+0.5)/H - 1) * tan(fovy/2)
    // the distance is 3 * sqrt(1 + u^2 + v^2). Pixel (16,15) is inside the
    // triangle near its lower left corner, which is the most off-axis part
    // of the geometry and therefore where the difference is largest.
    const uint32_t x = 16;
    const uint32_t y = 15;
    const float tanHalfFovy = std::tan(kPi / 6.f);
    const float u =
        (2.f * (float(x) + 0.5f) / float(kSize) - 1.f) * tanHalfFovy;
    const float v =
        (2.f * (float(y) + 0.5f) / float(kSize) - 1.f) * tanHalfFovy;
    const float expected = 3.f * std::sqrt(1.f + u * u + v * v);
    const float measured = depth[size_t(y) * kSize + x];
    std::printf("off-axis depth %.4f (expected %.4f, i.e. %.1f%% more than the "
                "plane distance)\n",
        measured,
        expected,
        100.f * (expected / 3.f - 1.f));
    CHECK_MSG(std::isfinite(measured),
        "the off-axis probe does not hit the triangle");
    CHECK_MSG(close(measured, expected, 0.05f),
        "the off-axis depth is not the distance along the ray");

    // Where nothing is hit the depth is infinite, not zero: a zero would put
    // the background *at* the camera.
    const float corner = depth[size_t(kSize - 1) * kSize];
    std::printf("corner depth %.4f\n", corner);
    CHECK_MSG(std::isinf(corner) && corner > 0.f,
        "a pixel that hits nothing must report an infinite depth");
  }

  anariUnmapFrame(d, s.frame, "channel.depth");
  test_scene::release(s);
  return failures;
}

// --- anari.frame_resize -----------------------------------------------------
//
// Resizing a frame keeps the camera: the same fovy and aspect see the same
// part of the world, so the *fraction* of the frame the triangle covers has to
// survive the resize, as does the color of the pixel at its center. Both are
// derived, not remembered - and both would break if the device kept rendering
// at the old resolution or forgot to rewrite the camera, whose film-back
// derivation depends on the frame's aspect.

int testFrameResize(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};

  auto s = test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);

  auto renderAtSize = [&](uint32_t w, uint32_t h) {
    const uint32_t size[2] = {w, h};
    anariSetParameter(d, s.frame, "size", ANARI_UINT32_VEC2, size);
    anariCommitParameters(d, s.frame);
    s.width = w;
    s.height = h;

    anariRenderFrame(d, s.frame);
    anariFrameReady(d, s.frame, ANARI_WAIT);

    uint32_t mw = 0, mh = 0;
    ANARIDataType t = ANARI_UNKNOWN;
    const auto *pixels =
        (const float *)anariMapFrame(d, s.frame, "channel.color", &mw, &mh, &t);

    struct Result
    {
      uint32_t width{0}, height{0};
      float coverage{0.f};
      float center[3]{0.f, 0.f, 0.f};
    } r;
    r.width = mw;
    r.height = mh;
    if (pixels && t == ANARI_FLOAT32_VEC4 && mw == w && mh == h) {
      unsigned covered = 0;
      for (size_t i = 0; i < size_t(mw) * mh; ++i) {
        if (pixels[4 * i + 1] > 0.05f) // green: the background has none
          ++covered;
      }
      r.coverage = float(covered) / float(size_t(mw) * mh);
      const size_t c = size_t(mh / 2) * mw + mw / 2;
      for (int k = 0; k < 3; ++k)
        r.center[k] = pixels[4 * c + k];
    }
    anariUnmapFrame(d, s.frame, "channel.color");
    return r;
  };

  const auto small = renderAtSize(kSize, kSize);
  CHECK_MSG(small.width == kSize && small.height == kSize,
      "the frame did not report the size it was given");
  CHECK_MSG(small.coverage > 0.f, "nothing was rendered");

  const uint32_t bigger = 3 * kSize / 2; // 96: not a multiple of the tile size
  const auto large = renderAtSize(bigger, bigger);
  std::printf("resized %ux%u -> %ux%u, coverage %.4f -> %.4f\n",
      kSize,
      kSize,
      large.width,
      large.height,
      small.coverage,
      large.coverage);
  CHECK_MSG(large.width == bigger && large.height == bigger,
      "the resized frame did not report its new size");
  CHECK_MSG(std::fabs(large.coverage - small.coverage) < 0.02f,
      "the triangle must cover the same fraction of the frame after a resize");
  for (int k = 0; k < 3; ++k) {
    CHECK_MSG(std::fabs(large.center[k] - albedo[k]) < 0.02f,
        "the resized frame does not render the material color");
  }

  // ... and back down again, so a shrink is covered too.
  const auto again = renderAtSize(kSize, kSize);
  std::printf("shrunk back: %ux%u, coverage %.4f\n",
      again.width,
      again.height,
      again.coverage);
  CHECK_MSG(again.width == kSize && again.height == kSize,
      "the frame did not shrink back");
  CHECK_MSG(std::fabs(again.coverage - small.coverage) < 0.01f,
      "shrinking back must reproduce the original image");

  test_scene::release(s);
  return failures;
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "triangle_matte";

  ANARILibrary lib = anariLoadLibrary("moonray", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: anariLoadLibrary(\"moonray\") returned null\n");
    return 1;
  }

  ANARIDevice device = anariNewDevice(lib, "default");
  if (!device) {
    std::printf("FAIL: anariNewDevice(lib, \"default\") returned null\n");
    anariUnloadLibrary(lib);
    return 1;
  }

  if (mode == "triangle_matte")
    testTriangleMatte(device);
  else if (mode == "frame_color_float")
    testFrameColorFloat(device);
  else if (mode == "frame_depth")
    testFrameDepth(device);
  else if (mode == "frame_resize")
    testFrameResize(device);
  else {
    std::printf("FAIL: unknown mode '%s'\n", mode.c_str());
    ++failures;
  }

  anariRelease(device, device);
  anariUnloadLibrary(lib);

  if (failures == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", failures);
  return 1;
}
