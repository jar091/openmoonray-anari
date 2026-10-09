// Milestone 13 tests: does the device behave like an ANARI device when it is
// watched, when it is lied to, and when it is asked what it can do?
//
//   * debug_layer     - the SDK's debug device wraps this one, checks every
//                       call against the spec, and reports what leaked. The
//                       image has to come out the same as without it.
//   * malformed_input - wrong types, missing parameters, impossible values and
//                       unknown subtypes, none of which may crash the device
//                       or turn into an error; each must be a warning, and a
//                       correct scene must still render afterwards.
//   * advertised_subtypes - everything MoonrayDefinitions.json advertises can
//                       be created and made valid, and things it does not
//                       advertise report themselves invalid. This is the
//                       "no false capability declarations" check.
//
// Usage: integration_compliance
//            <debug_layer|malformed_input|advertised_subtypes>

#include "CrashTrace.h"
#include "TriangleScene.h"

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int errors = 0;
int warnings = 0;
std::vector<std::string> messages;

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
  messages.emplace_back(message ? message : "");
  if (severity <= ANARI_SEVERITY_ERROR) {
    std::printf("[anari][error] %s\n", message);
    ++errors;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::printf("[anari][warn ] %s\n", message);
    ++warnings;
  }
}

bool close(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

// --- anari.debug_layer ------------------------------------------------------

int testDebugLayer()
{
  ANARILibrary moonrayLib = anariLoadLibrary("moonray", statusFunc, nullptr);
  ANARILibrary debugLib = anariLoadLibrary("debug", statusFunc, nullptr);
  if (!moonrayLib || !debugLib) {
    std::printf("FAIL: could not load the moonray and debug libraries\n");
    return ++failures;
  }

  ANARIDevice wrapped = anariNewDevice(moonrayLib, "default");
  ANARIDevice device = anariNewDevice(debugLib, "debug");
  CHECK_MSG(wrapped && device, "device creation failed");
  if (!wrapped || !device)
    return failures;

  anariSetParameter(device, device, "wrappedDevice", ANARI_DEVICE, &wrapped);
  anariCommitParameters(device, device);

  // Everything from here on goes through the debug device, which checks each
  // call against the specification before forwarding it.
  const int errorsBefore = errors;
  auto scene =
      test_scene::build(device, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);
  auto image = test_scene::renderToFloat(scene);

  CHECK_MSG(!image.empty(), "the debug-wrapped device rendered nothing");
  if (!image.empty()) {
    const float *center = test_scene::centerPixel(image, kSize);
    std::printf("through the debug layer: %.4f %.4f %.4f\n",
        center[0],
        center[1],
        center[2]);
    for (int k = 0; k < 3; ++k) {
      CHECK_MSG(close(center[k], kAlbedo[k], 0.02f),
          "the debug layer must not change what the device renders");
    }
  }

  test_scene::release(scene);
  anariRelease(device, device);
  anariRelease(wrapped, wrapped);

  std::printf("debug layer reported %d errors\n", errors - errorsBefore);
  CHECK_MSG(errors == errorsBefore,
      "the debug layer found a specification violation in this device's use "
      "of ANARI");

  anariUnloadLibrary(debugLib);
  anariUnloadLibrary(moonrayLib);
  return failures;
}

// --- anari.malformed_input --------------------------------------------------

int testMalformedInput(ANARIDevice d)
{
  // 1. Unknown subtypes. Every one of these must come back as an object that
  //    reports itself invalid rather than as a null handle or a crash.
  struct
  {
    const char *kind;
    ANARIObject object;
  } unknown[] = {
      {"geometry", (ANARIObject)anariNewGeometry(d, "definitely-not-a-thing")},
      {"material", (ANARIObject)anariNewMaterial(d, "unobtainium")},
      {"light", (ANARIObject)anariNewLight(d, "candle")},
      {"camera", (ANARIObject)anariNewCamera(d, "pinhole")},
      {"sampler", (ANARIObject)anariNewSampler(d, "image7D")},
      {"volume", (ANARIObject)anariNewVolume(d, "smoke")},
      {"spatialField", (ANARIObject)anariNewSpatialField(d, "amr")},
  };

  for (auto &u : unknown) {
    CHECK_MSG(u.object != nullptr,
        "an unknown subtype must still produce a handle");
    if (!u.object)
      continue;
    anariCommitParameters(d, u.object);
    // ANARI_BOOL is int8_t; reading it into an int gets the neighbouring
    // bytes as well.
    int8_t validBool = 1;
    anariGetProperty(d,
        u.object,
        "valid",
        ANARI_BOOL,
        &validBool,
        sizeof(validBool),
        ANARI_WAIT);
    std::printf("unknown %s subtype: valid = %d\n", u.kind, (int)validBool);
    CHECK_MSG(validBool == 0,
        "an unknown subtype must report itself invalid, not pretend to work");
    anariRelease(d, u.object);
  }

  // 2. A geometry with the wrong array type, and one with no positions at
  //    all. Both must warn and stay invalid.
  {
    const float wrong[8] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};
    ANARIGeometry g = anariNewGeometry(d, "triangle");
    ANARIArray1D badPositions =
        anariNewArray1D(d, wrong, nullptr, nullptr, ANARI_FLOAT32_VEC2, 4);
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &badPositions);
    const int warningsBefore = warnings;
    anariCommitParameters(d, g);
    int8_t valid = 1;
    anariGetProperty(
        d, g, "valid", ANARI_BOOL, &valid, sizeof(valid), ANARI_WAIT);
    CHECK_MSG(valid == 0,
        "a triangle geometry with float2 positions must be invalid");
    CHECK_MSG(warnings > warningsBefore,
        "the device must say why it rejected the geometry");
    anariRelease(d, badPositions);
    anariRelease(d, g);
  }

  {
    ANARIGeometry g = anariNewGeometry(d, "triangle");
    anariCommitParameters(d, g);
    int8_t valid = 1;
    anariGetProperty(
        d, g, "valid", ANARI_BOOL, &valid, sizeof(valid), ANARI_WAIT);
    CHECK_MSG(
        valid == 0, "a geometry with no vertex.position must be invalid");
    anariRelease(d, g);
  }

  // 3. Out-of-range indices: the device rejects them rather than handing
  //    MoonRay memory to read past the end of the vertex array.
  {
    const float vertices[9] = {
        -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f};
    const uint32_t indices[3] = {0, 1, 99};
    ANARIGeometry g = anariNewGeometry(d, "triangle");
    ANARIArray1D p =
        anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
    ANARIArray1D i =
        anariNewArray1D(d, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 1);
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &p);
    anariSetParameter(d, g, "primitive.index", ANARI_ARRAY1D, &i);
    anariCommitParameters(d, g);
    int8_t valid = 1;
    anariGetProperty(
        d, g, "valid", ANARI_BOOL, &valid, sizeof(valid), ANARI_WAIT);
    CHECK_MSG(valid == 0, "an out-of-range index must invalidate the geometry");
    anariRelease(d, p);
    anariRelease(d, i);
    anariRelease(d, g);
  }

  // 4. Nonsense on a valid subtype: a negative sphere radius, a zero-area quad
  //    light, an empty environment map.
  {
    ANARIGeometry g = anariNewGeometry(d, "sphere");
    const float center[3] = {0.f, 0.f, 0.f};
    ANARIArray1D p =
        anariNewArray1D(d, center, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
    const float radius = -1.f;
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &p);
    anariSetParameter(d, g, "radius", ANARI_FLOAT32, &radius);
    anariCommitParameters(d, g);
    int8_t valid = 1;
    anariGetProperty(
        d, g, "valid", ANARI_BOOL, &valid, sizeof(valid), ANARI_WAIT);
    CHECK_MSG(valid == 0, "a negative sphere radius must be rejected");
    anariRelease(d, p);
    anariRelease(d, g);
  }

  {
    ANARILight l = anariNewLight(d, "quad");
    const float edge[3] = {1.f, 0.f, 0.f};
    anariSetParameter(d, l, "edge1", ANARI_FLOAT32_VEC3, edge);
    anariSetParameter(d, l, "edge2", ANARI_FLOAT32_VEC3, edge); // parallel
    anariCommitParameters(d, l);
    int8_t valid = 1;
    anariGetProperty(
        d, l, "valid", ANARI_BOOL, &valid, sizeof(valid), ANARI_WAIT);
    CHECK_MSG(valid == 0, "a quad light with parallel edges has no area");
    anariRelease(d, l);
  }

  // 5. After all of that, a correct scene must still render correctly - the
  //    device must not have been left in a broken state.
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);
  auto image = test_scene::renderToFloat(scene);
  CHECK_MSG(!image.empty(), "the device stopped rendering after bad input");
  if (!image.empty()) {
    const float *center = test_scene::centerPixel(image, kSize);
    std::printf("after the malformed input: %.4f %.4f %.4f\n",
        center[0],
        center[1],
        center[2]);
    for (int k = 0; k < 3; ++k) {
      CHECK_MSG(close(center[k], kAlbedo[k], 0.02f),
          "a correct scene must still render correctly after bad input");
    }
  }
  test_scene::release(scene);

  std::printf("%d warning(s), %d error(s)\n", warnings, errors);
  CHECK_MSG(errors == 0,
      "malformed input must be reported as warnings, not errors: an ANARI "
      "device is not allowed to fail on input it can reject");
  CHECK_MSG(warnings > 0, "the device rejected things without saying so");

  return failures;
}

// --- anari.advertised_subtypes ----------------------------------------------
//
// Every subtype in MoonrayDefinitions.json, given a minimal correct set of
// parameters, must report itself valid. The list is written out here on
// purpose: it is what the device *claims*, and the test is what the device
// *does*.

int testAdvertisedSubtypes(ANARIDevice d)
{
  auto checkValid = [&](const char *what, ANARIObject o) {
    anariCommitParameters(d, o);
    int8_t valid = 0;
    anariGetProperty(
        d, o, "valid", ANARI_BOOL, &valid, sizeof(valid), ANARI_WAIT);
    std::printf("%-28s valid = %d\n", what, (int)valid);
    CHECK_MSG(valid == 1,
        "an advertised subtype must work when it is given what it needs");
    anariRelease(d, o);
  };

  // Cameras.
  {
    ANARICamera c = anariNewCamera(d, "perspective");
    checkValid("camera perspective", (ANARIObject)c);
    c = anariNewCamera(d, "orthographic");
    checkValid("camera orthographic", (ANARIObject)c);
  }

  // Geometries.
  const float vertices[9] = {-1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f};
  ANARIArray1D positions =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  {
    ANARIGeometry g = anariNewGeometry(d, "triangle");
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &positions);
    checkValid("geometry triangle", (ANARIObject)g);

    g = anariNewGeometry(d, "sphere");
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &positions);
    checkValid("geometry sphere", (ANARIObject)g);

    g = anariNewGeometry(d, "curve");
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &positions);
    checkValid("geometry curve", (ANARIObject)g);
  }

  // Materials.
  {
    ANARIMaterial m = anariNewMaterial(d, "matte");
    checkValid("material matte", (ANARIObject)m);
    m = anariNewMaterial(d, "physicallyBased");
    checkValid("material physicallyBased", (ANARIObject)m);
  }

  // Lights. The directional light needs an irradiance to be worth anything,
  // and says so by reporting itself invalid without one.
  {
    ANARILight l = anariNewLight(d, "directional");
    const float irradiance = 1.f;
    anariSetParameter(d, l, "irradiance", ANARI_FLOAT32, &irradiance);
    checkValid("light directional", (ANARIObject)l);

    l = anariNewLight(d, "point");
    checkValid("light point", (ANARIObject)l);

    l = anariNewLight(d, "quad");
    const float e1[3] = {1.f, 0.f, 0.f};
    const float e2[3] = {0.f, 1.f, 0.f};
    anariSetParameter(d, l, "edge1", ANARI_FLOAT32_VEC3, e1);
    anariSetParameter(d, l, "edge2", ANARI_FLOAT32_VEC3, e2);
    checkValid("light quad", (ANARIObject)l);

    l = anariNewLight(d, "hdri");
    const float radiance[3] = {1.f, 1.f, 1.f};
    ANARIArray2D map =
        anariNewArray2D(d, radiance, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1, 1);
    anariSetParameter(d, l, "radiance", ANARI_ARRAY2D, &map);
    checkValid("light hdri", (ANARIObject)l);
    anariRelease(d, map);
  }

  // Sampler.
  {
    const float texels[12] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 1.f, 1.f, 1.f, 0.f};
    ANARIArray2D image =
        anariNewArray2D(d, texels, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2, 2);
    ANARISampler s = anariNewSampler(d, "image2D");
    anariSetParameter(d, s, "image", ANARI_ARRAY2D, &image);
    checkValid("sampler image2D", (ANARIObject)s);
    anariRelease(d, image);
  }

  anariRelease(d, positions);

  // The renderer and the core objects.
  {
    ANARIRenderer r = anariNewRenderer(d, "default");
    checkValid("renderer default", (ANARIObject)r);
  }

  return failures;
}

// --- anari.scene_churn ------------------------------------------------------
//
// One device, many scenes, built and thrown away in turn - which is what the
// ANARI CTS does (it loads the device once and runs every test case through
// it) and what any application with a scene browser does. Nothing in ANARI
// says a device may only ever be given one world.
//
// The case that broke: a world with no surfaces at all. RDL2 has no way to
// delete a SceneObject, so the geometry of the previous scene stays in the
// SceneContext; emptying the layer left MoonRay shading primitives whose
// assignment IDs no longer existed, and scene_rdl2 threw IndexError on a
// render thread - std::terminate, and the whole process gone with no message.
//
// A world with no surfaces is a legal ANARI world, and it renders the
// background.

int testSceneChurn(ANARIDevice d)
{
  const float albedo[3] = {0.8f, 0.2f, 0.2f};
  const float background[4] = {0.f, 0.f, 1.f, 1.f};
  constexpr uint32_t kSize = 32;

  const auto centerIs = [&](const std::vector<float> &image,
                            const float expected[3],
                            const char *what) {
    if (image.empty()) {
      std::printf("FAIL: %s produced no frame\n", what);
      ++failures;
      return;
    }
    const float *c = test_scene::centerPixel(image, kSize);
    std::printf("%s: center %.4f %.4f %.4f (expected %.4f %.4f %.4f)\n",
        what,
        c[0],
        c[1],
        c[2],
        expected[0],
        expected[1],
        expected[2]);
    for (int k = 0; k < 3; ++k) {
      if (std::fabs(c[k] - expected[k]) > 0.02f) {
        std::printf("FAIL: %s did not render what it should\n", what);
        ++failures;
        return;
      }
    }
  };

  // 1. A scene with a surface.
  {
    auto scene =
        test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);
    centerIs(test_scene::renderToFloat(scene), albedo, "first scene");
    test_scene::release(scene);
  }

  // 2. A scene whose world holds no surfaces at all: every pixel is the
  //    background, including the middle of the frame.
  {
    auto scene =
        test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);
    anariUnsetParameter(d, scene.world, "surface");
    anariCommitParameters(d, scene.world);
    centerIs(
        test_scene::renderToFloat(scene), background, "world with no surfaces");
    test_scene::release(scene);
  }

  // 3. And a scene after that still renders its surface - the empty world
  //    must not have left the device unusable.
  {
    auto scene =
        test_scene::build(d, albedo, background, ANARI_FLOAT32_VEC4, kSize);
    centerIs(test_scene::renderToFloat(scene),
        albedo,
        "scene after the empty world");
    test_scene::release(scene);
  }

  return failures;
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "debug_layer";

  test_crash::install();

  if (mode == "debug_layer") {
    testDebugLayer();
  } else {
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

    if (mode == "malformed_input")
      testMalformedInput(device);
    else if (mode == "advertised_subtypes")
      testAdvertisedSubtypes(device);
    else if (mode == "scene_churn")
      testSceneChurn(device);
    else {
      std::printf("unknown mode: %s\n", mode.c_str());
      return 2;
    }

    anariRelease(device, device);
    anariUnloadLibrary(lib);
  }

  if (failures == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", failures);
  return 1;
}
