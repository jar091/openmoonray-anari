// Milestone 12 tests: the transferFunction1D volume over a structuredRegular
// spatial field.
//
// The expectations are Beer-Lambert, derived from the scene:
//
//   * a purely absorbing volume of extinction sigma over a path of length L
//     transmits exp(-sigma*L) of what is behind it, and the device composites
//     the renderer's background behind the render, so the background channel
//     *is* that transmittance;
//   * doubling the opacity squares the transmittance, which holds whatever
//     the path length is - the assertion that cannot be fooled by getting the
//     grid's extent wrong;
//   * and the same volume at twice the voxel resolution transmits the same,
//     because ANARI's opacity is per unit of path and not per voxel. That one
//     is what a MoonRay built against an Embree without ray masks fails
//     (docs/WINDOWS_PORT.md).
//
// The second mode also pins down *where* the field lands in world space: half
// the grid is opaque and half is transparent, so a mirrored or shifted grid
// transform shows up immediately.
//
// Usage: integration_volume <volume_absorption|volume_transfer_function>

#include "CrashTrace.h"
#include "TriangleScene.h"

#include <anari/anari.h>

#include <cmath>
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
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kGrid = 16;   // voxels per axis, by default
constexpr float kExtent = 2.f;   // the world-space size of the volume's box
constexpr float kCameraDistance = 3.f;

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

float pixelsPerUnit()
{
  return float(kSize) / (2.f * kCameraDistance * std::tan(kPi / 6.f));
}

// A voxel *occupies* one spacing, so the box a grid of N voxels covers spans
// half a spacing beyond the outermost voxel centers. Placing the first center
// there makes the volume exactly [-1,1] in every axis, whatever N is - which
// is what lets the same scene be rendered at two resolutions and compared.
inline float spacingFor(uint32_t grid)
{
  return kExtent / float(grid);
}
inline float originFor(uint32_t grid)
{
  return -0.5f * kExtent + 0.5f * spacingFor(grid);
}

// Builds the shared scene without its triangle, plus a volume over `values`.
struct VolumeScene
{
  test_scene::TriangleScene base;
  ANARISpatialField field{nullptr};
  ANARIVolume volume{nullptr};
};

VolumeScene buildVolume(
    ANARIDevice d, const std::vector<float> &values, uint32_t grid = kGrid)
{
  VolumeScene s;
  s.base =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // The triangle would sit inside the volume; this test is about the volume.
  anariUnsetParameter(d, s.base.world, "surface");

  ANARIArray3D data = anariNewArray3D(
      d, values.data(), nullptr, nullptr, ANARI_FLOAT32, grid, grid, grid);

  const float o = originFor(grid);
  const float sp = spacingFor(grid);
  const float origin[3] = {o, o, o};
  const float spacing[3] = {sp, sp, sp};

  s.field = anariNewSpatialField(d, "structuredRegular");
  anariSetParameter(d, s.field, "data", ANARI_ARRAY3D, &data);
  anariSetParameter(d, s.field, "origin", ANARI_FLOAT32_VEC3, origin);
  anariSetParameter(d, s.field, "spacing", ANARI_FLOAT32_VEC3, spacing);
  anariCommitParameters(d, s.field);
  anariRelease(d, data);

  s.volume = anariNewVolume(d, "transferFunction1D");
  anariSetParameter(d, s.volume, "value", ANARI_SPATIAL_FIELD, &s.field);
  const float valueRange[2] = {0.f, 1.f};
  anariSetParameter(d, s.volume, "valueRange", ANARI_FLOAT32_BOX1, valueRange);
  // Black: the volume only absorbs, so nothing it does depends on the lights.
  const float black[3] = {0.f, 0.f, 0.f};
  anariSetParameter(d, s.volume, "color", ANARI_FLOAT32_VEC3, black);
  anariCommitParameters(d, s.volume);

  ANARIArray1D volumes =
      anariNewArray1D(d, &s.volume, nullptr, nullptr, ANARI_VOLUME, 1);
  anariSetParameter(d, s.base.world, "volume", ANARI_ARRAY1D, &volumes);
  anariCommitParameters(d, s.base.world);
  anariRelease(d, volumes);

  // Volume integration is stochastic; more samples, less noise.
  const int pixelSamples = 64;
  anariSetParameter(
      d, s.base.renderer, "pixelSamples", ANARI_INT32, &pixelSamples);
  anariCommitParameters(d, s.base.renderer);

  return s;
}

void release(ANARIDevice d, VolumeScene &s)
{
  anariRelease(d, s.volume);
  anariRelease(d, s.field);
  test_scene::release(s.base);
}

// --- anari.volume_absorption ------------------------------------------------

void testVolumeAbsorption(ANARIDevice d)
{
  const std::vector<float> values(
      size_t(kGrid) * kGrid * kGrid, 1.f); // a uniform field

  auto scene = buildVolume(d, values);

  // opacity 0.5 per unit distance over the 2 units the box is deep.
  const float pathLength = kExtent;
  float opacity = 0.5f;
  anariSetParameter(d, scene.volume, "opacity", ANARI_FLOAT32, &opacity);
  anariCommitParameters(d, scene.volume);

  auto image = test_scene::renderToFloat(scene.base);
  const float *center = test_scene::centerPixel(image, kSize);
  const float expected = std::exp(-opacity * pathLength);
  std::printf(
      "sigma=%.2f: rgba %.4f %.4f %.4f %.4f -> background transmitted %.4f "
      "(expected %.4f)\n",
      opacity,
      center[0],
      center[1],
      center[2],
      center[3],
      center[2],
      expected);
  CHECK_MSG(close(center[2], expected, 0.03f),
      "a purely absorbing volume must transmit exp(-sigma*L) of the "
      "background");
  CHECK_MSG(center[0] < 0.02f && center[1] < 0.02f,
      "a black volume must not add any color of its own");

  // Twice the opacity is the square of the transmittance - true whatever the
  // path length is, which is what makes this the assertion that matters.
  const float thin = center[2];
  opacity = 1.f;
  anariSetParameter(d, scene.volume, "opacity", ANARI_FLOAT32, &opacity);
  anariCommitParameters(d, scene.volume);

  image = test_scene::renderToFloat(scene.base);
  const float *denser = test_scene::centerPixel(image, kSize);
  std::printf("sigma=%.2f: %.4f (expected %.4f = the square of %.4f)\n",
      opacity,
      denser[2],
      thin * thin,
      thin);
  CHECK_MSG(close(denser[2], thin * thin, 0.03f),
      "doubling the opacity must square the transmittance");

  // A corner of the frame misses the volume entirely: the background, whole.
  const float *corner = image.data();
  std::printf("corner %.4f %.4f %.4f\n", corner[0], corner[1], corner[2]);
  CHECK_MSG(close(corner[2], 1.f, 0.01f),
      "outside the volume the background must come through unattenuated");

  release(d, scene);

  // The same volume at twice the resolution: the box, the field and the
  // opacity are unchanged, so the transmittance must be unchanged too. This
  // is the assertion that catches an extinction applied per *voxel* instead
  // of per unit length - which is exactly what a MoonRay built against an
  // Embree without ray masks produced (docs/WINDOWS_PORT.md).
  const uint32_t fineGrid = 2 * kGrid;
  const std::vector<float> fineValues(
      size_t(fineGrid) * fineGrid * fineGrid, 1.f);
  auto fine = buildVolume(d, fineValues, fineGrid);
  opacity = 0.5f;
  anariSetParameter(d, fine.volume, "opacity", ANARI_FLOAT32, &opacity);
  anariCommitParameters(d, fine.volume);

  image = test_scene::renderToFloat(fine.base);
  const float *fineCenter = test_scene::centerPixel(image, kSize);
  std::printf("at %ux the resolution: %.4f (%.4f at %ux)\n",
      fineGrid,
      fineCenter[2],
      thin,
      kGrid);
  CHECK_MSG(close(fineCenter[2], thin, 0.02f),
      "the same volume at twice the resolution must transmit the same: the "
      "opacity is per unit length, not per voxel");

  release(d, fine);
}

// --- anari.volume_transfer_function -----------------------------------------

void testVolumeTransferFunction(ANARIDevice d)
{
  // The field is 0 in the half with x < 0 and 1 in the half with x > 0, and
  // the transfer function maps 0 to fully transparent and 1 to opaque. So the
  // rendered image says exactly where the grid landed - a mirrored or shifted
  // transform cannot pass.
  std::vector<float> values(size_t(kGrid) * kGrid * kGrid, 0.f);
  for (uint32_t k = 0; k < kGrid; ++k) {
    for (uint32_t j = 0; j < kGrid; ++j) {
      for (uint32_t i = kGrid / 2; i < kGrid; ++i)
        values[(size_t(k) * kGrid + j) * kGrid + i] = 1.f;
    }
  }

  auto scene = buildVolume(d, values);

  const float opacities[2] = {0.f, 2.f};
  ANARIArray1D opacityArray =
      anariNewArray1D(d, opacities, nullptr, nullptr, ANARI_FLOAT32, 2);
  anariSetParameter(d, scene.volume, "opacity", ANARI_ARRAY1D, &opacityArray);
  anariCommitParameters(d, scene.volume);
  anariRelease(d, opacityArray);

  auto image = test_scene::renderToFloat(scene.base);
  const float ppu = pixelsPerUnit();
  const auto sample = [&](float x) {
    const uint32_t col = uint32_t(kSize / 2.f + x * ppu);
    return image[4 * (size_t(kSize / 2) * kSize + col) + 2];
  };

  const float leftSide = sample(-0.7f);
  const float rightSide = sample(0.7f);
  const float expected = std::exp(-2.f * 2.f); // sigma 2 over 2 units
  std::printf("left (transparent) %.4f, right (opaque) %.4f (expected %.4f)\n",
      leftSide,
      rightSide,
      expected);
  CHECK_MSG(close(leftSide, 1.f, 0.02f),
      "where the transfer function says opacity 0 the background must come "
      "through untouched");
  CHECK_MSG(close(rightSide, expected, 0.02f),
      "where it says opacity 2 the background must be attenuated by "
      "exp(-2*L)");

  release(d, scene);
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "volume_absorption";

  // Volumes are the first thing this device asks of MoonRay's volume path;
  // when that path dies, a symbolized stack is the difference between a
  // diagnosis and a guess (tests/common/CrashTrace.h).
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

  if (mode == "volume_absorption")
    testVolumeAbsorption(device);
  else if (mode == "volume_transfer_function")
    testVolumeTransferFunction(device);
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
