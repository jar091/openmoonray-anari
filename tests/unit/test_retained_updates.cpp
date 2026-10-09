// Milestone 6 completion: the two retained-object-model tests that need a
// real render to be observable (MILESTONES.md M6 deferred them to M7 rather
// than faking observability).
//
//   commit_snapshot    — a parameter change has no effect until
//                        anariCommitParameters() is called on that object
//   dirty_propagation  — committing a change deep in the scene graph reaches
//                        the frame without re-committing everything above it
//
// Both work through the PUBLIC ANARI API and judge the result by the rendered
// image, which is the only place where "took effect" is externally visible.
//
// Usage: unit_retained_updates <commit_snapshot|dirty_propagation>

#include "TriangleScene.h"

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
  return std::fabs(a - b) <= tol;
}

// --- unit.commit_snapshot ---------------------------------------------------

void testCommitSnapshot(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float newColor[3] = {0.1f, 0.7f, 0.3f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};

  auto scene = test_scene::build(
      d, albedo, background, ANARI_FLOAT32_VEC4, kSize);

  auto image = test_scene::renderToFloat(scene);
  CHECK_MSG(!image.empty(), "first render produced no image");
  if (image.empty())
    return;

  const float *c = test_scene::centerPixel(image, kSize);
  CHECK_MSG(close(c[0], albedo[0], 0.05f) && close(c[1], albedo[1], 0.05f),
      "the first render does not show the material color");

  // Set a new color but do NOT commit: the device must keep rendering the
  // committed snapshot.
  anariSetParameter(d, scene.material, "color", ANARI_FLOAT32_VEC3, newColor);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  std::printf("uncommitted: %.4f %.4f %.4f (expected the old %.2f %.2f %.2f)\n",
      c[0], c[1], c[2], albedo[0], albedo[1], albedo[2]);
  CHECK_MSG(close(c[0], albedo[0], 0.05f) && close(c[1], albedo[1], 0.05f)
          && close(c[2], albedo[2], 0.05f),
      "an uncommitted parameter change must not affect the render");

  // Now commit it: the same render must show the new color.
  anariCommitParameters(d, scene.material);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  std::printf("committed:   %.4f %.4f %.4f (expected %.2f %.2f %.2f)\n",
      c[0], c[1], c[2], newColor[0], newColor[1], newColor[2]);
  CHECK_MSG(close(c[0], newColor[0], 0.05f) && close(c[1], newColor[1], 0.05f)
          && close(c[2], newColor[2], 0.05f),
      "a committed parameter change must affect the next render");

  test_scene::release(scene);
}

// --- unit.dirty_propagation -------------------------------------------------

void testDirtyPropagation(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};

  auto scene = test_scene::build(
      d, albedo, background, ANARI_FLOAT32_VEC4, kSize);

  auto image = test_scene::renderToFloat(scene);
  CHECK_MSG(!image.empty(), "first render produced no image");
  if (image.empty())
    return;

  // Coverage = pixels the triangle actually covers. It cannot be read off the
  // alpha channel: the background is composited under the render and is
  // itself opaque, so every pixel ends up with alpha 1. The background has no
  // green, so the material's green channel is what distinguishes them.
  auto coverage = [](const std::vector<float> &img) {
    unsigned n = 0;
    for (size_t i = 0; i < img.size() / 4; ++i) {
      if (img[4 * i + 1] > 0.05f)
        ++n;
    }
    return n;
  };

  const unsigned coverageBefore = coverage(image);
  CHECK_MSG(coverageBefore > 0, "nothing was rendered");

  // 1. A material change, committed on the *material* only. Neither the
  //    surface, the world nor the frame is touched.
  const float newColor[3] = {0.1f, 0.7f, 0.3f};
  anariSetParameter(d, scene.material, "color", ANARI_FLOAT32_VEC3, newColor);
  anariCommitParameters(d, scene.material);

  image = test_scene::renderToFloat(scene);
  const float *c = test_scene::centerPixel(image, kSize);
  std::printf("after material commit: %.4f %.4f %.4f\n", c[0], c[1], c[2]);
  CHECK_MSG(close(c[0], newColor[0], 0.05f) && close(c[1], newColor[1], 0.05f),
      "a material change must reach the frame through surface/world");

  // 2. A geometry change, committed on the *geometry* only: a triangle half
  //    the size covers measurably fewer pixels.
  const float smaller[3][3] = {
      {-0.5f, -0.5f, 0.f}, {0.5f, -0.5f, 0.f}, {0.f, 0.5f, 0.f}};
  ANARIArray1D positionArray =
      anariNewArray1D(d, smaller, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  anariSetParameter(
      d, scene.geometry, "vertex.position", ANARI_ARRAY1D, &positionArray);
  anariCommitParameters(d, scene.geometry);
  anariRelease(d, positionArray);

  image = test_scene::renderToFloat(scene);
  const unsigned coverageAfter = coverage(image);
  std::printf("coverage: %u pixels before, %u after shrinking the triangle\n",
      coverageBefore, coverageAfter);
  CHECK_MSG(coverageAfter > 0, "the shrunken triangle disappeared entirely");
  // Half the edge length is a quarter of the area; allow plenty of slack but
  // require a real, measurable change.
  CHECK_MSG(coverageAfter < coverageBefore / 2,
      "a geometry change must reach the frame");

  // 3. A light change, committed on the *light* only: no light, no image.
  const float dark[3] = {0.f, 0.f, 0.f};
  anariSetParameter(d, scene.light, "color", ANARI_FLOAT32_VEC3, dark);
  anariCommitParameters(d, scene.light);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  std::printf("after light commit: %.4f %.4f %.4f\n", c[0], c[1], c[2]);
  CHECK_MSG(c[0] < 0.02f && c[1] < 0.02f && c[2] < 0.02f,
      "a light change must reach the frame");

  test_scene::release(scene);
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::printf("usage: %s <commit_snapshot|dirty_propagation>\n", argv[0]);
    return 2;
  }
  const std::string mode = argv[1];

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

  if (mode == "commit_snapshot")
    testCommitSnapshot(device);
  else if (mode == "dirty_propagation")
    testDirtyPropagation(device);
  else {
    std::printf("unknown mode: %s\n", mode.c_str());
    return 2;
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
