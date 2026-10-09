// Milestone 16: what does the adapter cost per frame, and does an update stay
// incremental as the scene grows?
//
// The renderer dominates wall-clock time, so a benchmark that only timed
// `anariRenderFrame` would measure MoonRay, not this device. What is measured
// here instead is the *translation*: the device reports how many RDL2 objects
// each sync rewrote (`moonray.objectsUpdatedLastSync` /
// `.objectsUnchangedLastSync`), and those counts are the thing that has to
// stay constant when the scene grows and only one object changes.
//
// So this is both a benchmark and a test:
//
//   * timings are printed (they are hardware-dependent and asserting on them
//     would make the suite fragile), and
//   * the invariants are asserted:
//       - re-rendering an unchanged scene rewrites *nothing*;
//       - moving one instance rewrites a constant number of objects,
//         independent of how many instances there are;
//       - neither rebuilds the layer.
//
// A second argument makes the shared group's mesh large. That is what turns
// "how much does the translation copy" from a rounding error into a number:
// with a one-triangle prototype every phase costs the same, whatever the
// device does.
//
// Usage: bench_updates [instances] [grid cells per side]

#include "TriangleScene.h"

#include <anari/anari.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

float floatProperty(ANARIDevice d, const char *name)
{
  float value = -1.f;
  anariGetProperty(
      d, d, name, ANARI_FLOAT32, &value, sizeof(value), ANARI_WAIT);
  return value;
}

// Replaces the scene's single triangle with a `cells` x `cells` grid over the
// same [-1,1] square: 2*cells^2 triangles, (cells+1)^2 vertices, in the same
// place and facing the same way, so every derived expectation about the image
// still holds.
void makeGrid(ANARIDevice d, ANARIGeometry geometry, int cells)
{
  const int side = cells + 1;
  std::vector<float> positions;
  positions.reserve(size_t(side) * side * 3);
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      positions.push_back(-1.f + 2.f * float(x) / float(cells));
      positions.push_back(-1.f + 2.f * float(y) / float(cells));
      positions.push_back(0.f);
    }
  }

  std::vector<uint32_t> indices;
  indices.reserve(size_t(cells) * cells * 6);
  for (int y = 0; y < cells; ++y) {
    for (int x = 0; x < cells; ++x) {
      const uint32_t v0 = uint32_t(y * side + x);
      const uint32_t v1 = v0 + 1;
      const uint32_t v2 = v0 + uint32_t(side);
      const uint32_t v3 = v2 + 1;
      indices.push_back(v0);
      indices.push_back(v1);
      indices.push_back(v3);
      indices.push_back(v0);
      indices.push_back(v3);
      indices.push_back(v2);
    }
  }

  ANARIArray1D positionArray = anariNewArray1D(d,
      positions.data(),
      nullptr,
      nullptr,
      ANARI_FLOAT32_VEC3,
      positions.size() / 3);
  ANARIArray1D indexArray = anariNewArray1D(
      d, indices.data(), nullptr, nullptr, ANARI_UINT32_VEC3, indices.size() / 3);
  anariSetParameter(d, geometry, "vertex.position", ANARI_ARRAY1D, &positionArray);
  anariSetParameter(d, geometry, "primitive.index", ANARI_ARRAY1D, &indexArray);
  anariCommitParameters(d, geometry);
  anariRelease(d, positionArray);
  anariRelease(d, indexArray);

  std::printf("prototype: %zu triangles, %zu vertices\n",
      indices.size() / 3,
      positions.size() / 3);
}

int32_t intProperty(ANARIDevice d, const char *name)
{
  int32_t value = -1;
  anariGetProperty(
      d, d, name, ANARI_INT32, &value, sizeof(value), ANARI_WAIT);
  return value;
}

bool layerRebuilt(ANARIDevice d)
{
  int8_t value = 0;
  anariGetProperty(d,
      d,
      "moonray.layerRebuiltLastSync",
      ANARI_BOOL,
      &value,
      sizeof(value),
      ANARI_WAIT);
  return value != 0;
}

struct Timing
{
  double meanMs{0.0};
  double minMs{0.0};
};

template <typename F>
Timing measure(int iterations, F &&body)
{
  using clock = std::chrono::steady_clock;
  double total = 0.0;
  double best = 1e30;
  for (int i = 0; i < iterations; ++i) {
    const auto t0 = clock::now();
    body(i);
    const double ms =
        std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    total += ms;
    best = std::min(best, ms);
  }
  Timing t;
  t.meanMs = total / iterations;
  t.minMs = best;
  return t;
}

} // namespace

int main(int argc, const char **argv)
{
  const int instanceCount = argc > 1 ? std::atoi(argv[1]) : 64;
  const int gridCells = argc > 2 ? std::atoi(argv[2]) : 0;

  ANARILibrary lib = anariLoadLibrary("moonray", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: anariLoadLibrary(\"moonray\") returned null\n");
    return 1;
  }
  ANARIDevice d = anariNewDevice(lib, "default");
  if (!d) {
    std::printf("FAIL: anariNewDevice(lib, \"default\") returned null\n");
    anariUnloadLibrary(lib);
    return 1;
  }

  // A row of instances of one shared group: the geometry data exists once and
  // the instances are what the updates touch.
  std::vector<float> offsets(instanceCount);
  for (int i = 0; i < instanceCount; ++i)
    offsets[i] = -0.5f * instanceCount + float(i);

  auto scene = test_scene::buildInstanced(
      d, kAlbedo, kBackground, offsets, kSize, /*cameraDistance=*/40.f);

  if (gridCells > 0)
    makeGrid(d, scene.base.geometry, gridCells);

  std::printf("instances: %d\n", instanceCount);

  const auto first = measure(1, [&](int) {
    (void)test_scene::renderToFloat(scene.base);
  });
  const int32_t firstUpdated = intProperty(d, "moonray.objectsUpdatedLastSync");
  std::printf("first frame        %8.2f ms   (sync %6.3f ms, %d written)\n",
      first.meanMs,
      floatProperty(d, "moonray.syncMillisecondsLastFrame"),
      firstUpdated);

  // 1. Nothing changed.
  const auto unchanged = measure(5, [&](int) {
    (void)test_scene::renderToFloat(scene.base);
  });
  const int32_t unchangedUpdated =
      intProperty(d, "moonray.objectsUpdatedLastSync");
  const int32_t unchangedSkipped =
      intProperty(d, "moonray.objectsUnchangedLastSync");
  const double unchangedSyncMillis =
      floatProperty(d, "moonray.syncMillisecondsLastFrame");
  std::printf("unchanged re-render%8.2f ms   (sync %6.3f ms, %d written, %d skipped)\n",
      unchanged.meanMs,
      unchangedSyncMillis,
      unchangedUpdated,
      unchangedSkipped);
  CHECK_MSG(unchangedUpdated == 0,
      "re-rendering an unchanged scene must rewrite nothing");
  CHECK_MSG(!layerRebuilt(d),
      "re-rendering an unchanged scene must not rebuild the layer");

  // 2. One instance moved.
  const auto moved = measure(5, [&](int i) {
    test_scene::setInstanceOffset(
        d, scene.instances[0], offsets[0] + 0.01f * float(i + 1));
    (void)test_scene::renderToFloat(scene.base);
  });
  const int32_t movedUpdated = intProperty(d, "moonray.objectsUpdatedLastSync");
  const float movedSyncMs = floatProperty(d, "moonray.syncMillisecondsLastFrame");
  std::printf("one instance moved %8.2f ms   (sync %6.3f ms, %d written)\n",
      moved.meanMs,
      floatProperty(d, "moonray.syncMillisecondsLastFrame"),
      movedUpdated);
  CHECK_MSG(movedUpdated == 1,
      "moving one instance must rewrite the instancer and nothing else, "
      "whatever the instance count - its prototype is told to regenerate "
      "(rdl2::SceneObject::requestUpdate) rather than rewritten");
  CHECK_MSG(!layerRebuilt(d),
      "moving an instance must not rebuild the layer");

  // With a large prototype, "rewrites nothing" has to be visible in the time
  // as well as in the count: copying its vertex arrays used to make this sync
  // an order of magnitude more expensive than one with nothing to do. The
  // assertion is a ratio rather than a duration so it means the same thing on
  // any machine.
  if (gridCells > 0) {
    const float unchangedSyncMs = float(unchangedSyncMillis);
    std::printf("sync: %.3f ms to move an instance, %.3f ms with nothing to "
                "do (ratio %.1f)\n",
        movedSyncMs,
        unchangedSyncMs,
        unchangedSyncMs > 0.f ? movedSyncMs / unchangedSyncMs : 0.f);
    CHECK_MSG(movedSyncMs < 5.f * unchangedSyncMs + 0.5f,
        "moving one instance must not copy the prototype's geometry data");
  }

  // 3. The material changed.
  const auto material = measure(5, [&](int i) {
    const float color[3] = {0.8f, 0.2f + 0.01f * float(i), 0.2f};
    anariSetParameter(d, scene.base.material, "color", ANARI_FLOAT32_VEC3, color);
    anariCommitParameters(d, scene.base.material);
    (void)test_scene::renderToFloat(scene.base);
  });
  const int32_t materialUpdated =
      intProperty(d, "moonray.objectsUpdatedLastSync");
  std::printf("material changed   %8.2f ms   (sync %6.3f ms, %d written)\n",
      material.meanMs,
      floatProperty(d, "moonray.syncMillisecondsLastFrame"),
      materialUpdated);
  CHECK_MSG(materialUpdated == 1,
      "changing a material must rewrite exactly the material");

  test_scene::release(scene);
  anariRelease(d, d);
  anariUnloadLibrary(lib);

  if (failures == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", failures);
  return 1;
}
