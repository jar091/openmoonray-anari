// Milestone 15 test: MoonRay's execution modes through the device.
//
// MoonRay can render the same scene scalar, vectorized (ISPC on the CPU) or
// XPU (CPU + GPU through OptiX). The mode is a *renderer* choice, not a
// picture choice, so the assertion is that the image does not depend on it:
// each mode has to produce the Milestone 7 image, whose expectation is
// derived (a Lambertian surface of albedo a under irradiance pi renders a).
//
// The mode is asked for with the device parameter `moonray.executionMode` and
// read back with the property of the same name - and reading it back matters,
// because MoonRay falls back: "xpu" without a usable GPU (or a build without
// OptiX) becomes "vectorized", and "auto" becomes "scalar" for scenes with
// features vector mode does not support. The test therefore reports what it
// got and only insists that a *requested* mode is either honoured or reported
// as something else, never silently mislabelled.
//
// One process per mode: MoonRay has one renderer per process and the
// execution mode lives on its global options (docs/adr/0005).
//
// Usage: integration_execution_mode <scalar|vectorized|xpu|auto>

#include "CrashTrace.h"
#include "TriangleScene.h"

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
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

constexpr uint32_t kSize = 64;
const float kAlbedo[3] = {0.8f, 0.2f, 0.2f};
const float kBackground[4] = {0.f, 0.f, 1.f, 1.f};

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
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

} // namespace

int main(int argc, const char **argv)
{
  const std::string requested = argc > 1 ? argv[1] : "auto";

  test_crash::install();

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

  const char *mode = requested.c_str();
  anariSetParameter(device, device, "moonray.executionMode", ANARI_STRING, mode);
  anariCommitParameters(device, device);

  auto scene =
      test_scene::build(device, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);
  auto image = test_scene::renderToFloat(scene);

  const char *actual = nullptr;
  anariGetProperty(device,
      device,
      "moonray.executionMode",
      ANARI_STRING,
      &actual,
      sizeof(actual),
      ANARI_WAIT);
  std::printf("requested \"%s\", MoonRay ran \"%s\"\n",
      requested.c_str(),
      actual ? actual : "<none>");
  CHECK_MSG(actual != nullptr && actual[0] != '\0',
      "the device must report which execution mode MoonRay is running");

  if (requested == "scalar" || requested == "vectorized") {
    CHECK_MSG(actual && requested == actual,
        "scalar and vectorized are always available, so asking for one must "
        "get it");
  }
  if (requested == "xpu" && actual && requested != actual) {
    // Not a failure: MoonRay falls back when the GPU or the OptiX build is
    // missing. Saying so is the point of the property.
    std::printf(
        "note: XPU is not available in this build/machine, MoonRay fell back "
        "to \"%s\"\n",
        actual);
  }

  CHECK_MSG(!image.empty(), "nothing was rendered");
  if (!image.empty()) {
    const float *center = test_scene::centerPixel(image, kSize);
    std::printf("center %.4f %.4f %.4f (expected %.4f %.4f %.4f)\n",
        center[0],
        center[1],
        center[2],
        kAlbedo[0],
        kAlbedo[1],
        kAlbedo[2]);
    for (int k = 0; k < 3; ++k) {
      CHECK_MSG(close(center[k], kAlbedo[k], 0.02f),
          "the execution mode must not change the image: a Lambertian surface "
          "under irradiance pi renders its albedo, whichever way the renderer "
          "gets there");
    }

    const float *corner = image.data();
    CHECK_MSG(close(corner[2], 1.f, 0.01f) && corner[0] < 0.02f,
        "the corner must still be the renderer's background");
  }

  test_scene::release(scene);
  anariRelease(device, device);
  anariUnloadLibrary(lib);

  if (failures == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", failures);
  return 1;
}
