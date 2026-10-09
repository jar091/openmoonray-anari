// Milestone 8 ANARI update tests: retained-mode changes reach the frame
// without recreating the device, and a change to one object does not drag the
// whole scene through MoonRay again.
//
// Every mode below renders, changes exactly one thing through the public
// ANARI C API, renders again, and then asserts *two* things:
//
//   * the image changed the way the change implies (measured: coverage, the
//     centroid of the covered pixels, or the pixel value itself);
//   * the device only re-translated what it had to, read back from the
//     vendor device properties `moonray.objectsUpdatedLastSync` and
//     `moonray.layerRebuiltLastSync`.
//
// The second half is what "minimize full rebuilds" means in practice, and
// without it "the update works" would be satisfied by re-translating
// everything every frame.
//
// Usage: integration_updates <camera_update|material_update|light_update
//                            |instance_updates|shared_group
//                            |shared_group_attributes>

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

int objectsUpdated(ANARIDevice d)
{
  int n = -1;
  anariGetProperty(d,
      d,
      "moonray.objectsUpdatedLastSync",
      ANARI_INT32,
      &n,
      sizeof(n),
      ANARI_WAIT);
  return n;
}

int objectsUnchanged(ANARIDevice d)
{
  int n = -1;
  anariGetProperty(d,
      d,
      "moonray.objectsUnchangedLastSync",
      ANARI_INT32,
      &n,
      sizeof(n),
      ANARI_WAIT);
  return n;
}

bool layerRebuilt(ANARIDevice d)
{
  // ANARI_BOOL is one byte (anari/frontend/type_utility.h), so it has to be
  // read into a one-byte object - an int would keep three uninitialized
  // bytes and compare nonzero at random.
  int8_t rebuilt = -1;
  anariGetProperty(d,
      d,
      "moonray.layerRebuiltLastSync",
      ANARI_BOOL,
      &rebuilt,
      sizeof(rebuilt),
      ANARI_WAIT);
  return rebuilt != 0;
}

bool close(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

const float kAlbedo[3] = {0.8f, 0.2f, 0.2f};
const float kBackground[4] = {0.f, 0.f, 1.f, 1.f};

// --- anari.camera_update ----------------------------------------------------

void testCameraUpdate(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  auto image = test_scene::renderToFloat(scene);
  const unsigned before = test_scene::coverage(image);
  CHECK_MSG(before > 0, "nothing was rendered");

  // Rendering again with nothing changed must re-translate nothing at all.
  image = test_scene::renderToFloat(scene);
  std::printf("no-op re-render: %d updated, %d unchanged, layer rebuilt %d\n",
      objectsUpdated(d),
      objectsUnchanged(d),
      (int)layerRebuilt(d));
  CHECK_MSG(objectsUpdated(d) == 0,
      "an unchanged scene must not be re-translated");
  CHECK_MSG(objectsUnchanged(d) > 0, "nothing was tracked at all");
  CHECK_MSG(!layerRebuilt(d), "an unchanged scene must not rebuild the layer");
  CHECK_MSG(test_scene::coverage(image) == before,
      "an unchanged scene must render the same image");

  // Move the camera closer: the triangle has to cover more of the frame.
  const float closer[3] = {0.f, 0.f, 1.8f};
  anariSetParameter(d, scene.camera, "position", ANARI_FLOAT32_VEC3, closer);
  anariCommitParameters(d, scene.camera);

  image = test_scene::renderToFloat(scene);
  const unsigned after = test_scene::coverage(image);
  std::printf("coverage %u -> %u after moving the camera closer (%d updated)\n",
      before,
      after,
      objectsUpdated(d));
  CHECK_MSG(after > before * 3 / 2, "moving the camera closer must enlarge the triangle");
  CHECK_MSG(objectsUpdated(d) == 1, "only the camera should have been re-translated");
  CHECK_MSG(!layerRebuilt(d), "a camera move must not rebuild the layer");

  // A resized frame keeps the same field of view, so the *fraction* of the
  // frame the triangle covers is unchanged even though the camera object was
  // not touched.
  const uint32_t bigger[2] = {2 * kSize, 2 * kSize};
  anariSetParameter(d, scene.frame, "size", ANARI_UINT32_VEC2, bigger);
  anariCommitParameters(d, scene.frame);

  anariRenderFrame(d, scene.frame);
  anariFrameReady(d, scene.frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *pixels = (const float *)anariMapFrame(
      d, scene.frame, "channel.color", &w, &h, &t);
  std::vector<float> resized;
  if (pixels)
    resized.assign(pixels, pixels + size_t(w) * h * 4);
  anariUnmapFrame(d, scene.frame, "channel.color");

  CHECK_MSG(w == 2 * kSize && h == 2 * kSize, "the frame did not resize");
  const float fractionBefore = float(after) / float(kSize * kSize);
  const float fractionAfter =
      resized.empty() ? 0.f : float(test_scene::coverage(resized)) / float(w * h);
  std::printf("covered fraction %.4f at %ux%u, %.4f at %ux%u\n",
      fractionBefore,
      kSize,
      kSize,
      fractionAfter,
      w,
      h);
  CHECK_MSG(close(fractionAfter, fractionBefore, 0.01f),
      "a resize must not change the field of view");

  test_scene::release(scene);
}

// --- anari.material_update --------------------------------------------------

void testMaterialUpdate(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  auto image = test_scene::renderToFloat(scene);
  const float *c = test_scene::centerPixel(image, kSize);
  CHECK_MSG(close(c[0], kAlbedo[0], 0.05f), "the first render is not the material color");

  // 1. change a parameter of the existing material
  const float green[3] = {0.1f, 0.7f, 0.3f};
  anariSetParameter(d, scene.material, "color", ANARI_FLOAT32_VEC3, green);
  anariCommitParameters(d, scene.material);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  std::printf("after a material parameter change: %.3f %.3f %.3f (%d updated, layer %d)\n",
      c[0], c[1], c[2], objectsUpdated(d), (int)layerRebuilt(d));
  CHECK_MSG(close(c[0], green[0], 0.05f) && close(c[1], green[1], 0.05f),
      "the material parameter change did not reach the frame");
  CHECK_MSG(objectsUpdated(d) == 1, "only the material should have been re-translated");
  CHECK_MSG(!layerRebuilt(d),
      "changing a material's parameters must not rebuild the layer");

  // 2. replace the material object on the surface: the layer assignment
  //    itself changes, so this one *must* rebuild the layer.
  const float blue[3] = {0.1f, 0.2f, 0.9f};
  ANARIMaterial replacement = anariNewMaterial(d, "matte");
  anariSetParameter(d, replacement, "color", ANARI_FLOAT32_VEC3, blue);
  anariCommitParameters(d, replacement);
  anariSetParameter(d, scene.surface, "material", ANARI_MATERIAL, &replacement);
  anariCommitParameters(d, scene.surface);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  std::printf("after replacing the material: %.3f %.3f %.3f (%d updated, layer %d)\n",
      c[0], c[1], c[2], objectsUpdated(d), (int)layerRebuilt(d));
  CHECK_MSG(close(c[2], blue[2], 0.05f) && close(c[0], blue[0], 0.05f),
      "the replacement material did not reach the frame");
  CHECK_MSG(layerRebuilt(d),
      "replacing a surface's material must rebuild the layer assignment");

  anariRelease(d, replacement);
  test_scene::release(scene);
}

// --- anari.light_update -----------------------------------------------------

void testLightUpdate(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  auto image = test_scene::renderToFloat(scene);
  const float *c = test_scene::centerPixel(image, kSize);
  const float red = c[0];
  CHECK_MSG(close(red, kAlbedo[0], 0.05f), "the first render is not the material color");

  // Doubling the irradiance doubles the outgoing radiance: L = a * E / pi.
  const float irradiance = 2.f * test_scene::kPi;
  anariSetParameter(d, scene.light, "irradiance", ANARI_FLOAT32, &irradiance);
  anariCommitParameters(d, scene.light);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  std::printf("center red %.3f -> %.3f after doubling the irradiance (%d updated)\n",
      red, c[0], objectsUpdated(d));
  CHECK_MSG(close(c[0], 2.f * red, 0.05f),
      "doubling the irradiance must double the radiance");
  CHECK_MSG(objectsUpdated(d) == 1, "only the light should have been re-translated");
  CHECK_MSG(!layerRebuilt(d), "a light parameter change must not rebuild the layer");

  // Turning the light off must leave the geometry unlit - and the background
  // still visible, which is what separates "no light" from "no render".
  const float black[3] = {0.f, 0.f, 0.f};
  anariSetParameter(d, scene.light, "color", ANARI_FLOAT32_VEC3, black);
  anariCommitParameters(d, scene.light);

  image = test_scene::renderToFloat(scene);
  c = test_scene::centerPixel(image, kSize);
  CHECK_MSG(c[0] < 0.02f && c[1] < 0.02f && c[2] < 0.02f,
      "a black light must leave the surface unlit");
  const float *corner = image.data() + 4 * size_t(kSize - 1) * kSize;
  CHECK_MSG(close(corner[2], kBackground[2], 0.02f),
      "the background must still be there");

  test_scene::release(scene);
}

// --- anari.instance_updates -------------------------------------------------

void testInstanceUpdates(ANARIDevice d)
{
  auto scene = test_scene::buildInstanced(
      d, kAlbedo, kBackground, {0.f}, kSize, /*cameraDistance=*/8.f);

  auto image = test_scene::renderToFloat(scene.base);
  const unsigned coverageBefore = test_scene::coverage(image);
  const float centroidBefore = test_scene::coverageCentroidX(image, kSize);
  CHECK_MSG(coverageBefore > 0, "nothing was rendered");
  CHECK_MSG(close(centroidBefore, kSize / 2.f, 2.f),
      "the un-transformed instance is not centered");

  // Move the instance to the right. Nothing else is touched.
  test_scene::setInstanceOffset(d, scene.instances[0], 2.f);

  image = test_scene::renderToFloat(scene.base);
  const unsigned coverageAfter = test_scene::coverage(image);
  const float centroidAfter = test_scene::coverageCentroidX(image, kSize);
  std::printf("centroid x %.2f -> %.2f, coverage %u -> %u (%d updated, layer %d)\n",
      centroidBefore, centroidAfter, coverageBefore, coverageAfter,
      objectsUpdated(d), (int)layerRebuilt(d));

  CHECK_MSG(centroidAfter > centroidBefore + 5.f,
      "moving the instance must move the triangle in the image");
  CHECK_MSG(coverageAfter > coverageBefore * 3 / 4,
      "moving the instance must not change how much of it is visible");
  CHECK_MSG(objectsUpdated(d) == 1,
      "only the instanced geometry should have been re-translated");
  CHECK_MSG(!layerRebuilt(d),
      "moving an instance must not rebuild the layer");

  test_scene::release(scene);
}

// --- anari.shared_group -----------------------------------------------------

void testSharedGroup(ANARIDevice d)
{
  // One instance first, to measure what a single triangle covers.
  auto single = test_scene::buildInstanced(
      d, kAlbedo, kBackground, {0.f}, kSize, /*cameraDistance=*/8.f);
  auto image = test_scene::renderToFloat(single.base);
  const unsigned one = test_scene::coverage(image);
  CHECK_MSG(one > 0, "nothing was rendered");
  test_scene::release(single);

  // The same group, instanced three times side by side. The group - and with
  // it the geometry data - exists once; only the transforms differ.
  auto scene = test_scene::buildInstanced(
      d, kAlbedo, kBackground, {-2.5f, 0.f, 2.5f}, kSize, /*cameraDistance=*/8.f);
  image = test_scene::renderToFloat(scene.base);
  const unsigned three = test_scene::coverage(image);
  std::printf("coverage: %u for one instance, %u for three\n", one, three);
  CHECK_MSG(three > 2.5f * one,
      "three instances of a shared group must cover about three times as much");

  // ... and they must be three separate triangles, not one smeared blob: the
  // gaps between them stay background.
  auto litColumn = [&](uint32_t x) {
    for (uint32_t y = 0; y < kSize; ++y) {
      if (image[4 * (size_t(y) * kSize + x) + 1] > 0.05f)
        return true;
    }
    return false;
  };
  // Where the instances land is arithmetic, not guesswork: with the camera 8
  // units back and a 60 degree vertical field of view the frame spans
  // +-8*tan(30) = +-4.62 world units, i.e. 64/9.24 = 6.93 pixels per unit.
  // Each triangle's base runs from x-1 to x+1, so the three at -2.5, 0 and
  // 2.5 occupy columns [7.8,21.6], [25.1,38.9] and [42.4,56.2] - which leaves
  // gaps at 23 and 41.
  const float pixelsPerUnit = kSize / (2.f * 8.f * std::tan(test_scene::kPi / 6.f));
  const auto column = [&](float x) {
    return uint32_t(kSize / 2.f + x * pixelsPerUnit);
  };
  CHECK_MSG(litColumn(column(-2.5f)), "the left instance is missing");
  CHECK_MSG(litColumn(column(0.f)), "the middle instance is missing");
  CHECK_MSG(litColumn(column(2.5f)), "the right instance is missing");
  CHECK_MSG(!litColumn(column(-1.25f)),
      "the gap between the left and middle instance is filled");
  CHECK_MSG(!litColumn(column(1.25f)),
      "the gap between the middle and right instance is filled");

  // Moving one instance of the shared group moves only that copy.
  test_scene::setInstanceOffset(d, scene.instances[0], -2.5f, 2.0f);
  auto moved = test_scene::renderToFloat(scene.base);
  std::printf("after moving one of three: %d updated, layer rebuilt %d\n",
      objectsUpdated(d), (int)layerRebuilt(d));
  CHECK_MSG(test_scene::coverage(moved) > 2.5f * one,
      "moving one instance must not lose the others");
  // One object: the instancer. Its prototype is regenerated by MoonRay too -
  // a referenced geometry follows its referrer - and the device tells it so
  // with rdl2::SceneObject::requestUpdate() rather than by rewriting its
  // vertex arrays, which is a bool instead of a copy (MoonrayScene::
  // syncSurfaces, docs/PERFORMANCE.md).
  CHECK_MSG(objectsUpdated(d) == 1,
      "moving an instance should re-translate the instancer and nothing else");
  CHECK_MSG(!layerRebuilt(d), "moving an instance must not rebuild the layer");

  test_scene::release(scene);
}

// --- anari.shared_group_attributes ------------------------------------------
//
// A shared group whose material reads a *geometry* attribute, with an instance
// moved between the two renders. This is the case the prototype/instancer
// split makes delicate: MoonRay regenerates a referenced geometry when its
// referrer changes, but its update pass only carries the material's attribute
// set for geometries whose own assignment changed - so a prototype that is
// regenerated without being told it needs updating comes back without the
// primitive attributes its material asked for, and the surface renders black.
//
// The expectation is derived the same way anari.vertex_colors derives it: at
// the centroid of a triangle whose three vertices are red, green and blue, all
// three barycentric weights are 1/3, and with irradiance pi a matte surface
// renders the interpolated attribute itself.

void testSharedGroupAttributes(ANARIDevice d)
{
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kCameraDistance = 8.f;
  const float ppu =
      float(kSize) / (2.f * kCameraDistance * std::tan(kPi / 6.f));

  auto scene = test_scene::buildInstanced(
      d, kAlbedo, kBackground, {-2.5f, 0.f, 2.5f}, kSize, kCameraDistance);

  const float colors[9] = {
      1.f, 0.f, 0.f, // vertex 0 at (-1,-1)
      0.f, 1.f, 0.f, // vertex 1 at ( 1,-1)
      0.f, 0.f, 1.f}; // vertex 2 at ( 0, 1)
  ANARIArray1D colorArray =
      anariNewArray1D(d, colors, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  anariSetParameter(
      d, scene.base.geometry, "vertex.color", ANARI_ARRAY1D, &colorArray);
  anariCommitParameters(d, scene.base.geometry);
  anariRelease(d, colorArray);

  anariSetParameter(d, scene.base.material, "color", ANARI_STRING, "color");
  anariCommitParameters(d, scene.base.material);

  // The centroid of the instance at world x, in pixels. The triangle's
  // centroid sits at y = -1/3 in its own space.
  const auto centroidOf = [&](const std::vector<float> &image, float x) {
    const uint32_t px = uint32_t(kSize / 2.f + x * ppu);
    const uint32_t py = uint32_t(kSize / 2.f - ppu / 3.f);
    return image.data() + 4 * (size_t(py) * kSize + px);
  };

  const auto expectThirds = [&](const std::vector<float> &image,
                                float x,
                                const char *what) {
    const float *c = centroidOf(image, x);
    std::printf("%s at x=%.1f: %.4f %.4f %.4f (expected 0.333 each)\n",
        what,
        x,
        c[0],
        c[1],
        c[2]);
    for (int k = 0; k < 3; ++k) {
      CHECK_MSG(close(c[k], 1.f / 3.f, 0.06f),
          "an instanced surface whose material reads a geometry attribute "
          "must render that attribute");
    }
  };

  auto image = test_scene::renderToFloat(scene.base);
  expectThirds(image, -2.5f, "before the move");
  expectThirds(image, 2.5f, "before the move");

  // Move one instance. The prototype's data does not change - only its
  // instancer does.
  test_scene::setInstanceOffset(d, scene.instances[0], -3.5f);

  image = test_scene::renderToFloat(scene.base);
  std::printf("after the move: %d object(s) updated, layer %s\n",
      objectsUpdated(d),
      layerRebuilt(d) ? "rebuilt" : "unchanged");
  expectThirds(image, -3.5f, "after the move");
  expectThirds(image, 2.5f, "after the move");

  CHECK_MSG(!layerRebuilt(d), "moving an instance must not rebuild the layer");

  test_scene::release(scene);
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "camera_update";

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

  if (mode == "camera_update")
    testCameraUpdate(device);
  else if (mode == "material_update")
    testMaterialUpdate(device);
  else if (mode == "light_update")
    testLightUpdate(device);
  else if (mode == "instance_updates")
    testInstanceUpdates(device);
  else if (mode == "shared_group")
    testSharedGroup(device);
  else if (mode == "shared_group_attributes")
    testSharedGroupAttributes(device);
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
