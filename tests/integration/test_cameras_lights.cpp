// Milestone 10 tests: the orthographic camera and the point and quad lights.
//
// Each mode measures the property that defines the mapping rather than
// "something appeared":
//
//   * an orthographic camera does not shrink things with distance, so the
//     same geometry covers the same pixels from twice as far away, and its
//     `height` is literally the visible world height;
//   * a point light of intensity I at distance d illuminates a surface facing
//     it with irradiance E = I/d^2, so a Lambertian surface of albedo a
//     renders a*I/(pi*d^2) - the inverse-square law is the assertion;
//   * a quad light of radiance L, seen from far enough away that it subtends
//     a small solid angle, illuminates the same surface with
//     E = L*A*cos/d^2.
//
//   * an environment map of constant radiance L illuminates that surface with
//     E = pi*L, so it renders a*L, and a map that is lit over half the sphere
//     renders half of that when the split plane contains the normal - which
//     is what pins down where `up` and `direction` put the image.
//
// Usage: integration_cameras_lights
//            <orthographic_camera|point_light|quad_light|hdri_light>

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

const float kAlbedo[3] = {0.8f, 0.2f, 0.2f};
const float kBackground[4] = {0.f, 0.f, 1.f, 1.f};

// --- anari.orthographic_camera ----------------------------------------------

void testOrthographicCamera(ANARIDevice d)
{
  // The defining property of an orthographic camera is that size does not
  // depend on distance, so the test renders the same scene from two
  // distances and compares. The two renders use *separate scenes* on
  // purpose: see the note at the end of this function.
  auto renderAt = [&](float z) {
    auto scene =
        test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

    ANARICamera ortho = anariNewCamera(d, "orthographic");
    const float position[3] = {0.f, 0.f, z};
    const float direction[3] = {0.f, 0.f, -1.f};
    const float up[3] = {0.f, 1.f, 0.f};
    const float height = 4.f;
    const float aspect = 1.f;
    anariSetParameter(d, ortho, "position", ANARI_FLOAT32_VEC3, position);
    anariSetParameter(d, ortho, "direction", ANARI_FLOAT32_VEC3, direction);
    anariSetParameter(d, ortho, "up", ANARI_FLOAT32_VEC3, up);
    anariSetParameter(d, ortho, "height", ANARI_FLOAT32, &height);
    anariSetParameter(d, ortho, "aspect", ANARI_FLOAT32, &aspect);
    anariCommitParameters(d, ortho);

    anariSetParameter(d, scene.frame, "camera", ANARI_CAMERA, &ortho);
    anariCommitParameters(d, scene.frame);

    auto image = test_scene::renderToFloat(scene);
    const unsigned covered = test_scene::coverage(image);

    anariRelease(d, ortho);
    test_scene::release(scene);
    return covered;
  };

  const unsigned near = renderAt(3.f);
  CHECK_MSG(near > 0, "the orthographic camera rendered nothing");

  // The triangle occupies x,y in [-1,1] and covers half of that square (base
  // 2, height 2), i.e. 2 of the 16 square units a 4-unit-high orthographic
  // frame sees: 1/8 of the pixels. Anti-aliased edge pixels count as covered,
  // so the measurement sits slightly above the ideal fraction.
  const float covered = float(near) / float(kSize * kSize);
  std::printf("orthographic coverage %.4f of the frame (expected 0.125)\n",
      covered);
  CHECK_MSG(close(covered, 0.125f, 0.01f),
      "an orthographic camera of height 4 must show the triangle at 1/8 of "
      "the frame");

  // Three times as far away: identical, because the rays are parallel.
  const unsigned far = renderAt(9.f);
  std::printf("coverage %u at z=3, %u at z=9\n", near, far);
  CHECK_MSG(far > 0, "the distant orthographic camera rendered nothing");
  CHECK_MSG(std::abs(int(far) - int(near)) <= int(near) / 50,
      "an orthographic camera must not change size with distance");

  // Note (a known limitation, recorded in SUPPORTED_FEATURES.md): *moving* an
  // orthographic camera after the first render of a scene does not produce
  // the right image - a sideways move shifts the image several times too far
  // and a move along the view axis empties the frame, while the same camera
  // position is correct when it is the one the scene was first rendered with.
  // MoonRay fixes its render space to the camera transform in force when the
  // camera *object* changes (Scene::updateActiveCamera, and RenderContext's
  // "a camera change requires a geometry rebuild" comment), and the
  // orthographic ray generation evidently assumes the camera sits at that
  // origin. Perspective cameras move correctly (anari.camera_update), so this
  // is specific to the orthographic path and is not worked around here.
}

// --- anari.point_light ------------------------------------------------------

void testPointLight(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // Replace the directional light with a point light 4 units in front of the
  // triangle, on the axis. At the center of the triangle the light is
  // straight overhead, so E = I/d^2 and the rendered radiance is a*E/pi.
  const float distance = 4.f;
  const float intensity = 10.f;
  const float position[3] = {0.f, 0.f, distance};

  ANARILight point = anariNewLight(d, "point");
  anariSetParameter(d, point, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, point, "intensity", ANARI_FLOAT32, &intensity);
  anariCommitParameters(d, point);

  ANARIArray1D lights =
      anariNewArray1D(d, &point, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(d, scene.world, "light", ANARI_ARRAY1D, &lights);
  anariCommitParameters(d, scene.world);
  anariRelease(d, lights);

  auto image = test_scene::renderToFloat(scene);
  const float *c = test_scene::centerPixel(image, kSize);

  const float expected =
      kAlbedo[0] * intensity / (distance * distance) / kPi;
  std::printf("point light center red %.4f (expected %.4f)\n", c[0], expected);
  CHECK_MSG(close(c[0], expected, 0.02f),
      "a point light must follow the inverse-square law");

  // Twice as far away is a quarter as bright.
  const float farPosition[3] = {0.f, 0.f, 2.f * distance - 0.f};
  anariSetParameter(d, point, "position", ANARI_FLOAT32_VEC3, farPosition);
  anariCommitParameters(d, point);

  image = test_scene::renderToFloat(scene);
  const float *c2 = test_scene::centerPixel(image, kSize);
  const float expectedFar =
      kAlbedo[0] * intensity / (2.f * distance * 2.f * distance) / kPi;
  std::printf("at twice the distance %.4f (expected %.4f)\n", c2[0], expectedFar);
  CHECK_MSG(close(c2[0], expectedFar, 0.01f),
      "doubling the distance must quarter the illumination");

  anariRelease(d, point);
  test_scene::release(scene);
}

// --- anari.quad_light -------------------------------------------------------

void testQuadLight(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // A small quad 4 units in front of the triangle, facing it. Seen from the
  // triangle's center it subtends a small solid angle, so the irradiance is
  // E = L * A * cos(theta) / d^2 with theta = 0 on the axis.
  const float distance = 4.f;
  const float edge = 0.5f;
  const float radiance = 20.f;
  // The quad lies in the z = distance plane; edge1 x edge2 must point back at
  // the triangle, i.e. towards -z.
  const float position[3] = {-0.5f * edge, -0.5f * edge, distance};
  const float edge1[3] = {edge, 0.f, 0.f};
  const float edge2[3] = {0.f, -edge, 0.f};

  ANARILight quad = anariNewLight(d, "quad");
  anariSetParameter(d, quad, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, quad, "edge1", ANARI_FLOAT32_VEC3, edge1);
  anariSetParameter(d, quad, "edge2", ANARI_FLOAT32_VEC3, edge2);
  anariSetParameter(d, quad, "radiance", ANARI_FLOAT32, &radiance);
  anariCommitParameters(d, quad);

  ANARIArray1D lights =
      anariNewArray1D(d, &quad, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(d, scene.world, "light", ANARI_ARRAY1D, &lights);
  anariCommitParameters(d, scene.world);
  anariRelease(d, lights);

  auto image = test_scene::renderToFloat(scene);
  const float *c = test_scene::centerPixel(image, kSize);

  const float area = edge * edge;
  const float irradiance = radiance * area / (distance * distance);
  const float expected = kAlbedo[0] * irradiance / kPi;
  std::printf("quad light center red %.4f (expected about %.4f)\n",
      c[0], expected);
  // The small-angle form is an approximation of the exact integral over the
  // quad, so the bar is 5% rather than the near-exact one the point light
  // gets.
  CHECK_MSG(close(c[0], expected, 0.05f * expected + 0.002f),
      "a quad light's illumination must match L*A*cos/d^2");

  // The quad emits from one side only: flipping the normal must darken it.
  const float flipped2[3] = {0.f, edge, 0.f};
  anariSetParameter(d, quad, "edge2", ANARI_FLOAT32_VEC3, flipped2);
  anariCommitParameters(d, quad);

  image = test_scene::renderToFloat(scene);
  const float *back = test_scene::centerPixel(image, kSize);
  std::printf("with the quad facing away: %.4f\n", back[0]);
  CHECK_MSG(back[0] < 0.1f * expected,
      "a one-sided quad light must not illuminate from its back");

  anariRelease(d, quad);
  test_scene::release(scene);
}

// --- anari.hdri_light -------------------------------------------------------
//
// An environment of radiance L illuminates a surface with irradiance
// E = integral(L cos(theta) dw) over the visible hemisphere, which for a
// constant L is pi*L - so the Lambertian surface renders a*L, with the pi
// cancelling. That makes three derived measurements possible without ever
// eyeballing an image:
//
//   * a constant map renders a*L;
//   * a map that is white above the equator and black below renders a*L when
//     `up` is the surface normal, and black when `up` is flipped - the
//     vertical mapping;
//   * a map whose left half is white renders a*L/2 when `direction` points at
//     the surface (the split plane then contains the normal, halving the
//     cosine-weighted integral), and black when `direction` is rotated 90
//     degrees so that the *black* half covers the visible hemisphere. Which
//     way that rotation has to go is the azimuth handedness, and this is what
//     pins it down: the mirrored convention would render a*L there.

void testHdriLight(ANARIDevice d)
{
  constexpr uint32_t kMapW = 32;
  constexpr uint32_t kMapH = 16;
  const float L = 0.5f;

  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  ANARILight hdri = anariNewLight(d, "hdri");
  ANARIArray1D lights =
      anariNewArray1D(d, &hdri, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(d, scene.world, "light", ANARI_ARRAY1D, &lights);
  anariCommitParameters(d, scene.world);
  anariRelease(d, lights);

  // `radiance` is an ARRAY2D whose first element is the lower left texel.
  auto setMap = [&](const std::vector<float> &texels) {
    ANARIArray2D map = anariNewArray2D(
        d, texels.data(), nullptr, nullptr, ANARI_FLOAT32_VEC3, kMapW, kMapH);
    anariSetParameter(d, hdri, "radiance", ANARI_ARRAY2D, &map);
    anariRelease(d, map);
  };
  auto setVec3 = [&](const char *name, float x, float y, float z) {
    const float v[3] = {x, y, z};
    anariSetParameter(d, hdri, name, ANARI_FLOAT32_VEC3, v);
  };
  auto renderCenterRed = [&]() {
    auto image = test_scene::renderToFloat(scene);
    return test_scene::centerPixel(image, kSize)[0];
  };

  // --- 1. a constant environment ------------------------------------------
  std::vector<float> constant(size_t(kMapW) * kMapH * 3, L);
  setMap(constant);
  setVec3("up", 0.f, 0.f, 1.f);
  setVec3("direction", 1.f, 0.f, 0.f);
  anariCommitParameters(d, hdri);

  const float lit = kAlbedo[0] * L;
  float measured = renderCenterRed();
  std::printf("constant hdri: %.4f (expected %.4f)\n", measured, lit);
  CHECK_MSG(close(measured, lit, 0.04f * lit + 0.005f),
      "a constant environment of radiance L must render albedo*L");

  // --- 2. the vertical mapping: white above the equator --------------------
  // The triangle's normal is +Z (the camera looks at its front face from
  // +Z), so with up = +Z the white half *is* the visible hemisphere.
  std::vector<float> topHalf(size_t(kMapW) * kMapH * 3, 0.f);
  for (uint32_t y = kMapH / 2; y < kMapH; ++y) {
    for (uint32_t x = 0; x < kMapW; ++x) {
      for (int c = 0; c < 3; ++c)
        topHalf[3 * (size_t(y) * kMapW + x) + c] = L;
    }
  }
  setMap(topHalf);
  setVec3("up", 0.f, 0.f, 1.f);
  anariCommitParameters(d, hdri);

  measured = renderCenterRed();
  std::printf("upper half lit, up = +Z: %.4f (expected %.4f)\n", measured, lit);
  CHECK_MSG(close(measured, lit, 0.06f * lit + 0.005f),
      "the top of the map must be the +up hemisphere");

  setVec3("up", 0.f, 0.f, -1.f);
  anariCommitParameters(d, hdri);

  measured = renderCenterRed();
  std::printf("upper half lit, up = -Z: %.4f (expected ~0)\n", measured);
  CHECK_MSG(measured < 0.1f * lit,
      "flipping 'up' must move the lit half of the map out of sight");

  // --- 3. the azimuth mapping and its handedness ---------------------------
  std::vector<float> leftHalf(size_t(kMapW) * kMapH * 3, 0.f);
  for (uint32_t y = 0; y < kMapH; ++y) {
    for (uint32_t x = 0; x < kMapW / 2; ++x) {
      for (int c = 0; c < 3; ++c)
        leftHalf[3 * (size_t(y) * kMapW + x) + c] = L;
    }
  }
  setMap(leftHalf);
  // The pole now lies *in* the surface, so the equator runs through the
  // normal and the u = 1/2 meridian is where `direction` points.
  setVec3("up", 0.f, 1.f, 0.f);
  setVec3("direction", 0.f, 0.f, 1.f);
  anariCommitParameters(d, hdri);

  measured = renderCenterRed();
  std::printf("left half lit, direction = +Z: %.4f (expected %.4f)\n",
      measured, 0.5f * lit);
  CHECK_MSG(close(measured, 0.5f * lit, 0.1f * lit + 0.005f),
      "with the split meridian on the normal, half the hemisphere is lit");

  setVec3("direction", 1.f, 0.f, 0.f);
  anariCommitParameters(d, hdri);

  measured = renderCenterRed();
  std::printf("left half lit, direction = +X: %.4f (expected ~0)\n", measured);
  CHECK_MSG(measured < 0.15f * lit,
      "rotating 'direction' to +X must put the *black* half of the map over "
      "the visible hemisphere - the opposite azimuth handedness would light "
      "it fully");

  anariRelease(d, hdri);
  test_scene::release(scene);
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "orthographic_camera";

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

  if (mode == "orthographic_camera")
    testOrthographicCamera(device);
  else if (mode == "point_light")
    testPointLight(device);
  else if (mode == "quad_light")
    testQuadLight(device);
  else if (mode == "hdri_light")
    testHdriLight(device);
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
