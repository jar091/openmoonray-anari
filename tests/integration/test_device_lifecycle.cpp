// Milestone 13 test: creating and destroying devices, over and over.
//
// The ANARI CTS creates a fresh device for every one of its ~200 test cases in
// a single process, which is a use this device has to survive: nothing in the
// ANARI specification says a device may only be made once. MoonRay's global
// driver, on the other hand, is process-global state that moonray.exe
// initializes exactly once, so each device generation used to take that driver
// down and bring it back up.
//
// This test does the same thing the CTS does - build a device, render a frame
// small enough that the render itself is not the point, destroy it, repeat -
// and fails if any generation stops producing the frame the radiometry says it
// should (a Lambertian albedo under irradiance pi).
//
// The "shared" mode is the other half of the same question: one device, many
// scenes, which is what an application with a scene browser - and the CTS -
// does. Both modes assert the same derived expectation on every generation,
// so a frame that silently comes back empty is a failure, not a shrug.
//
// Usage: integration_device_lifecycle [generations] [fresh|shared]

#include "TriangleScene.h"

#include <anari/anari.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

constexpr uint32_t kSize = 32;
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
  }
}

bool close(float a, float b, float tol)
{
  return (a > b ? a - b : b - a) <= tol;
}

} // namespace

int main(int argc, const char **argv)
{
  const int generations = argc > 1 ? std::atoi(argv[1]) : 64;
  const bool sharedDevice = argc > 2 && std::string(argv[2]) == "shared";

  ANARILibrary lib = anariLoadLibrary("moonray", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: anariLoadLibrary(\"moonray\") returned null\n");
    return 1;
  }

  ANARIDevice shared = nullptr;
  if (sharedDevice) {
    shared = anariNewDevice(lib, "default");
    if (!shared) {
      std::printf("FAIL: anariNewDevice(lib, \"default\") returned null\n");
      anariUnloadLibrary(lib);
      return 1;
    }
  }

  for (int generation = 0; generation < generations; ++generation) {
    ANARIDevice device = shared ? shared : anariNewDevice(lib, "default");
    if (!device) {
      std::printf("FAIL: generation %d: anariNewDevice returned null\n",
          generation);
      anariUnloadLibrary(lib);
      return 1;
    }

    auto scene = test_scene::build(
        device, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);
    auto image = test_scene::renderToFloat(scene);
    const float *c = test_scene::centerPixel(image, kSize);

    for (int k = 0; k < 3; ++k) {
      if (!close(c[k], kAlbedo[k], 0.02f)) {
        std::printf(
            "FAIL: generation %d rendered %.4f %.4f %.4f, expected the albedo "
            "%.4f %.4f %.4f\n",
            generation,
            c[0],
            c[1],
            c[2],
            kAlbedo[0],
            kAlbedo[1],
            kAlbedo[2]);
        ++failures;
        break;
      }
    }

    test_scene::release(scene);
    if (!shared)
      anariRelease(device, device);

    // Progress, so that a hang or a crash names the generation it happened in
    // rather than leaving the log to be guessed at.
    if (generation % 8 == 0 || generation == generations - 1) {
      std::printf("generation %d/%d ok\n", generation + 1, generations);
      std::fflush(stdout);
    }
  }

  if (shared)
    anariRelease(shared, shared);
  anariUnloadLibrary(lib);

  if (failures == 0) {
    std::printf("PASS (%d %s generations)\n",
        generations,
        sharedDevice ? "scene" : "device");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", failures);
  return 1;
}
