// Milestone 11 tests: geometry coverage beyond positions and indices -
// spheres, curves, shading normals and vertex colors.
//
// Every expectation is derived from the scene, never remembered from a
// previous run. The shared scene puts the camera 3 units in front of the
// z = 0 plane with a 60 degree vertical field of view, so that plane spans
// 2*3*tan(30) = 3.4641 world units across the frame and one world unit is
// kSize/3.4641 pixels. The directional light shines along -Z with irradiance
// pi, which makes a Lambertian surface facing the camera render exactly its
// albedo (L = a*E*cos(theta)/pi).
//
// Usage: integration_geometry
//            <sphere_geometry|curve_geometry|vertex_normals|vertex_colors
//             |unusable_attributes>

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
constexpr float kPi = 3.14159265358979323846f;

// The camera the shared scene builds.
constexpr float kCameraDistance = 3.f;
const float kAlbedo[3] = {0.8f, 0.2f, 0.2f};
const float kBackground[4] = {0.f, 0.f, 1.f, 1.f};

// Pixels per world unit in the z = 0 plane.
float pixelsPerUnit()
{
  return float(kSize) / (2.f * kCameraDistance * std::tan(kPi / 6.f));
}

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

const float *pixelAt(const std::vector<float> &image, uint32_t x, uint32_t y)
{
  return image.data() + 4 * (size_t(y) * kSize + x);
}

// The bounding box of everything the geometry covers, judged by the green
// channel (the background has none).
struct Bounds
{
  uint32_t minX{kSize}, maxX{0}, minY{kSize}, maxY{0};
  unsigned count{0};
};

Bounds litBounds(const std::vector<float> &image, float threshold = 0.05f)
{
  Bounds b;
  for (uint32_t y = 0; y < kSize; ++y) {
    for (uint32_t x = 0; x < kSize; ++x) {
      if (image[4 * (size_t(y) * kSize + x) + 1] > threshold) {
        b.minX = std::min(b.minX, x);
        b.maxX = std::max(b.maxX, x);
        b.minY = std::min(b.minY, y);
        b.maxY = std::max(b.maxY, y);
        ++b.count;
      }
    }
  }
  return b;
}

// How many pixels of one column the geometry covers.
unsigned columnLitHeight(
    const std::vector<float> &image, uint32_t x, float threshold = 0.05f)
{
  unsigned n = 0;
  for (uint32_t y = 0; y < kSize; ++y) {
    if (image[4 * (size_t(y) * kSize + x) + 1] > threshold)
      ++n;
  }
  return n;
}

// Replaces the shared scene's triangle with `geometry`.
void useGeometry(ANARIDevice d, test_scene::TriangleScene &s, ANARIGeometry g)
{
  anariSetParameter(d, s.surface, "geometry", ANARI_GEOMETRY, &g);
  anariCommitParameters(d, s.surface);
}

// --- anari.sphere_geometry --------------------------------------------------
//
// A sphere of radius r at distance d projects to a disc whose angular radius
// is asin(r/d); in the image plane, where the half-height is tan(fovy/2), that
// disc has radius r/sqrt(d^2 - r^2). So the covered fraction of a square frame
// is pi * (r/sqrt(d^2-r^2) / tan(fovy/2))^2 / 4 - which is what the test
// measures, at two radii.

float expectedSphereCoverage(float radius)
{
  const float imagePlaneRadius =
      radius / std::sqrt(kCameraDistance * kCameraDistance - radius * radius);
  const float pixels = imagePlaneRadius / std::tan(kPi / 6.f) * (kSize / 2.f);
  return kPi * pixels * pixels / float(kSize * kSize);
}

void testSphereGeometry(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  const float center[3] = {0.f, 0.f, 0.f};
  ANARIGeometry sphere = anariNewGeometry(d, "sphere");
  ANARIArray1D positions =
      anariNewArray1D(d, center, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
  anariSetParameter(d, sphere, "vertex.position", ANARI_ARRAY1D, &positions);
  float radius = 1.f;
  anariSetParameter(d, sphere, "radius", ANARI_FLOAT32, &radius);
  anariCommitParameters(d, sphere);
  anariRelease(d, positions);

  useGeometry(d, scene, sphere);

  auto image = test_scene::renderToFloat(scene);
  auto bounds = litBounds(image);
  float measured = float(bounds.count) / float(kSize * kSize);
  float expected = expectedSphereCoverage(radius);
  std::printf("sphere r=1 covers %.4f of the frame (expected %.4f)\n",
      measured,
      expected);
  CHECK_MSG(close(measured, expected, 0.02f),
      "a sphere must cover the disc its projection says it does");

  // The point facing the camera has normal +Z, which is where the light comes
  // from, so the center pixel is the material color exactly.
  const float *c = test_scene::centerPixel(image, kSize);
  std::printf("sphere center %.4f %.4f %.4f (expected %.4f %.4f %.4f)\n",
      c[0], c[1], c[2], kAlbedo[0], kAlbedo[1], kAlbedo[2]);
  for (int k = 0; k < 3; ++k) {
    CHECK_MSG(close(c[k], kAlbedo[k], 0.02f),
        "the point of the sphere facing the camera is lit head-on, so it "
        "renders the material color");
  }

  // Half the radius is *not* half the coverage - it is the projection above.
  radius = 0.5f;
  anariSetParameter(d, sphere, "radius", ANARI_FLOAT32, &radius);
  anariCommitParameters(d, sphere);

  image = test_scene::renderToFloat(scene);
  bounds = litBounds(image);
  measured = float(bounds.count) / float(kSize * kSize);
  expected = expectedSphereCoverage(radius);
  std::printf("sphere r=0.5 covers %.4f (expected %.4f)\n", measured, expected);
  CHECK_MSG(close(measured, expected, 0.01f),
      "halving the radius must follow the projection, not a linear guess");

  // Per-sphere radii and positions: three spheres in a row, the middle one
  // twice the size of its neighbours. The outer two sit at -+1, so with radius
  // 0.25 they stay inside the +-1.732 world units the frame shows.
  const float centers[9] = {-1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  const float radii[3] = {0.25f, 0.5f, 0.25f};
  positions =
      anariNewArray1D(d, centers, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIArray1D radiusArray =
      anariNewArray1D(d, radii, nullptr, nullptr, ANARI_FLOAT32, 3);
  anariSetParameter(d, sphere, "vertex.position", ANARI_ARRAY1D, &positions);
  anariSetParameter(d, sphere, "vertex.radius", ANARI_ARRAY1D, &radiusArray);
  anariCommitParameters(d, sphere);
  anariRelease(d, positions);
  anariRelease(d, radiusArray);

  image = test_scene::renderToFloat(scene);
  const float ppu = pixelsPerUnit();
  const auto column = [&](float x) { return uint32_t(kSize / 2.f + x * ppu); };

  // Three separate spheres, not one smeared blob: the gaps between them
  // (x = -+0.625, halfway between the middle sphere's edge at -+0.5 and the
  // outer ones' at -+0.75) have to stay background.
  const float middleHeight = float(columnLitHeight(image, column(0.f)));
  const float leftHeight = float(columnLitHeight(image, column(-1.f)));
  const float rightHeight = float(columnLitHeight(image, column(1.f)));
  std::printf("three spheres: heights %.0f / %.0f / %.0f px, gaps %u / %u\n",
      leftHeight,
      middleHeight,
      rightHeight,
      columnLitHeight(image, column(-0.625f)),
      columnLitHeight(image, column(0.625f)));
  CHECK_MSG(leftHeight > 0 && middleHeight > 0 && rightHeight > 0,
      "one of the three spheres is missing");
  CHECK_MSG(columnLitHeight(image, column(-0.625f)) == 0
          && columnLitHeight(image, column(0.625f)) == 0,
      "the gaps between the three spheres are filled");
  // Every sphere is as tall as its own radius says, so the middle one is
  // twice its neighbours.
  CHECK_MSG(close(middleHeight, 2.f * 0.5f * ppu, 0.15f * ppu),
      "the middle sphere's diameter does not match its radius");
  CHECK_MSG(close(leftHeight, 2.f * 0.25f * ppu, 0.15f * ppu)
          && close(rightHeight, 2.f * 0.25f * ppu, 0.15f * ppu),
      "the outer spheres' diameters do not match their radii");

  anariRelease(d, sphere);
  test_scene::release(scene);
}

// --- anari.curve_geometry ---------------------------------------------------
//
// A linear curve is a tube of the given radius through its vertices. The test
// measures the silhouette: a curve from (-1,0,0) to (1,0,0) with radius r has
// to span 2 world units across and 2r up, in the z = 0 plane where one world
// unit is `pixelsPerUnit()` pixels.

void testCurveGeometry(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  const float vertices[6] = {-1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  float radius = 0.2f;

  ANARIGeometry curve = anariNewGeometry(d, "curve");
  ANARIArray1D positions =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
  anariSetParameter(d, curve, "vertex.position", ANARI_ARRAY1D, &positions);
  anariSetParameter(d, curve, "radius", ANARI_FLOAT32, &radius);
  anariCommitParameters(d, curve);
  anariRelease(d, positions);

  useGeometry(d, scene, curve);

  auto image = test_scene::renderToFloat(scene);
  auto bounds = litBounds(image);
  const float ppu = pixelsPerUnit();
  CHECK_MSG(bounds.count > 0, "the curve rendered nothing");

  float width = float(bounds.maxX - bounds.minX) + 1.f;
  float height = float(bounds.maxY - bounds.minY) + 1.f;
  // MoonRay's linear curves are *capsules*: the ends are rounded, so the
  // silhouette runs from the first vertex minus the radius to the last plus
  // the radius. (Measured: 46 px for a segment of 2 units and radius 0.2,
  // which is 2 + 2*0.2 units wide, not 2.)
  std::printf("curve r=0.2: %.0fx%.0f px (expected about %.1fx%.1f)\n",
      width,
      height,
      2.f * (1.f + radius) * ppu,
      2.f * radius * ppu);
  CHECK_MSG(close(width, 2.f * (1.f + radius) * ppu, 0.15f * ppu),
      "the curve must span the two world units between its vertices, plus a "
      "rounded cap of one radius at each end");
  CHECK_MSG(close(height, 2.f * radius * ppu, 0.25f * ppu),
      "the curve must be as thick as twice its radius");

  // The center of the curve faces the camera, so it renders the albedo.
  const float *c = test_scene::centerPixel(image, kSize);
  std::printf("curve center %.4f %.4f %.4f (albedo %.4f %.4f %.4f)\n",
      c[0], c[1], c[2], kAlbedo[0], kAlbedo[1], kAlbedo[2]);
  for (int k = 0; k < 3; ++k) {
    CHECK_MSG(close(c[k], kAlbedo[k], 0.06f),
        "the curve's surface facing the camera renders the material color");
  }

  // Half the radius is half the thickness, and the length does not change.
  radius = 0.1f;
  anariSetParameter(d, curve, "radius", ANARI_FLOAT32, &radius);
  anariCommitParameters(d, curve);

  image = test_scene::renderToFloat(scene);
  bounds = litBounds(image);
  const float thinHeight = float(bounds.maxY - bounds.minY) + 1.f;
  const float thinWidth = float(bounds.maxX - bounds.minX) + 1.f;
  std::printf("curve r=0.1: %.0fx%.0f px (expected about %.1fx%.1f)\n",
      thinWidth,
      thinHeight,
      2.f * (1.f + radius) * ppu,
      2.f * radius * ppu);
  CHECK_MSG(close(thinHeight, 2.f * radius * ppu, 0.25f * ppu),
      "halving the radius must halve the thickness");
  CHECK_MSG(close(thinWidth, 2.f * (1.f + radius) * ppu, 0.15f * ppu),
      "the curve's length must follow its vertices, with the caps shrinking "
      "along with the radius");

  // Two separate segments from four vertices, selected by primitive.index:
  // the middle of the frame must go back to being background.
  const float four[12] = {
      -1.5f, 0.f, 0.f, -0.5f, 0.f, 0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.f};
  const uint32_t segments[2] = {0, 2};
  positions =
      anariNewArray1D(d, four, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIArray1D index =
      anariNewArray1D(d, segments, nullptr, nullptr, ANARI_UINT32, 2);
  anariSetParameter(d, curve, "vertex.position", ANARI_ARRAY1D, &positions);
  anariSetParameter(d, curve, "primitive.index", ANARI_ARRAY1D, &index);
  anariCommitParameters(d, curve);
  anariRelease(d, positions);
  anariRelease(d, index);

  image = test_scene::renderToFloat(scene);
  const float *middle = test_scene::centerPixel(image, kSize);
  const float *leftSegment =
      pixelAt(image, uint32_t(kSize / 2.f - 1.f * ppu), kSize / 2);
  std::printf("two segments: middle %.4f, inside the left segment %.4f\n",
      middle[1],
      leftSegment[1]);
  CHECK_MSG(leftSegment[1] > 0.05f, "the left segment is missing");
  CHECK_MSG(middle[1] < 0.05f,
      "the gap between the two segments must not be filled - 'primitive.index' "
      "chose segments 0-1 and 2-3, not a single polyline");

  anariRelease(d, curve);
  test_scene::release(scene);
}

// --- anari.vertex_normals ---------------------------------------------------
//
// A shading normal turned away from the light dims the surface by its cosine
// and nothing else: with irradiance pi from +Z, a Lambertian surface whose
// shading normal makes an angle theta with +Z renders albedo*cos(theta).

void testVertexNormals(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // Without normals the triangle is flat and faces the camera: the reference.
  auto image = test_scene::renderToFloat(scene);
  const float *flat = test_scene::centerPixel(image, kSize);
  std::printf("flat triangle center red %.4f (expected %.4f)\n",
      flat[0],
      kAlbedo[0]);
  CHECK_MSG(close(flat[0], kAlbedo[0], 0.02f),
      "the flat triangle must render its albedo");

  // The same triangle with every shading normal tilted 60 degrees towards +X.
  const float theta = kPi / 3.f;
  const float n[3] = {std::sin(theta), 0.f, std::cos(theta)};
  const float normals[9] = {
      n[0], n[1], n[2], n[0], n[1], n[2], n[0], n[1], n[2]};
  ANARIArray1D normalArray =
      anariNewArray1D(d, normals, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  anariSetParameter(
      d, scene.geometry, "vertex.normal", ANARI_ARRAY1D, &normalArray);
  anariCommitParameters(d, scene.geometry);
  anariRelease(d, normalArray);

  image = test_scene::renderToFloat(scene);
  const float *tilted = test_scene::centerPixel(image, kSize);
  const float expected = kAlbedo[0] * std::cos(theta);
  std::printf("tilted normals center red %.4f (expected %.4f = albedo*cos60)\n",
      tilted[0],
      expected);
  CHECK_MSG(close(tilted[0], expected, 0.02f),
      "a shading normal 60 degrees from the light must dim the surface by "
      "cos(60)");

  test_scene::release(scene);
}

// --- anari.vertex_colors ----------------------------------------------------
//
// `vertex.color` plus a material whose color names the "color" attribute: the
// rendered color is the barycentric interpolation of the three vertex colors,
// and with irradiance pi it *is* that color, with nothing else in the way.

void testVertexColors(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  const float colors[9] = {
      1.f, 0.f, 0.f, // vertex 0 at (-1,-1)
      0.f, 1.f, 0.f, // vertex 1 at ( 1,-1)
      0.f, 0.f, 1.f}; // vertex 2 at ( 0, 1)
  ANARIArray1D colorArray =
      anariNewArray1D(d, colors, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  anariSetParameter(
      d, scene.geometry, "vertex.color", ANARI_ARRAY1D, &colorArray);
  anariCommitParameters(d, scene.geometry);
  anariRelease(d, colorArray);

  anariSetParameter(d, scene.material, "color", ANARI_STRING, "color");
  anariCommitParameters(d, scene.material);

  auto image = test_scene::renderToFloat(scene);
  const float ppu = pixelsPerUnit();

  // The centroid is (0, -1/3), where all three weights are 1/3.
  const uint32_t cx = uint32_t(kSize / 2.f);
  const uint32_t cy = uint32_t(kSize / 2.f - ppu / 3.f);
  const float *centroid = pixelAt(image, cx, cy);
  std::printf("centroid %.4f %.4f %.4f (expected 0.333 each)\n",
      centroid[0],
      centroid[1],
      centroid[2]);
  for (int k = 0; k < 3; ++k) {
    CHECK_MSG(close(centroid[k], 1.f / 3.f, 0.05f),
        "at the centroid every vertex color contributes a third");
  }

  // A point with barycentric weights (0.8, 0.1, 0.1) is at
  // 0.8*(-1,-1) + 0.1*(1,-1) + 0.1*(0,1) = (-0.7, -0.8).
  const uint32_t nx = uint32_t(kSize / 2.f - 0.7f * ppu);
  const uint32_t ny = uint32_t(kSize / 2.f - 0.8f * ppu);
  const float *nearRed = pixelAt(image, nx, ny);
  std::printf("near the red vertex %.4f %.4f %.4f (expected 0.8 0.1 0.1)\n",
      nearRed[0],
      nearRed[1],
      nearRed[2]);
  CHECK_MSG(close(nearRed[0], 0.8f, 0.08f),
      "the red vertex must dominate where its barycentric weight is 0.8");
  CHECK_MSG(close(nearRed[1], 0.1f, 0.05f) && close(nearRed[2], 0.1f, 0.05f),
      "the other two vertex colors must contribute a tenth each");

  test_scene::release(scene);
}

// --- anari.unusable_attributes ----------------------------------------------
//
// An optional attribute the device cannot use must cost the scene that
// attribute, not the geometry. The ANARI CTS found the opposite: a scalar
// `vertex.attribute0` (which the specification allows and which this device
// now reads as the u coordinate) made the whole mesh vanish, because the
// unusable-attribute path invalidated the geometry. The triangle here has to
// go on rendering its albedo through every one of these.

void expectTriangleStillRenders(
    test_scene::TriangleScene &scene, const char *what)
{
  auto image = test_scene::renderToFloat(scene);
  const float *c = test_scene::centerPixel(image, kSize);
  std::printf("%s: center %.4f %.4f %.4f (albedo %.4f %.4f %.4f)\n",
      what,
      c[0],
      c[1],
      c[2],
      kAlbedo[0],
      kAlbedo[1],
      kAlbedo[2]);
  for (int k = 0; k < 3; ++k) {
    CHECK_MSG(close(c[k], kAlbedo[k], 0.02f),
        "an attribute this device cannot use must be dropped, not take the "
        "geometry with it");
  }
}

void testUnusableAttributes(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // A one-component attribute0. ANARI allows float, float2, float3 and float4
  // attributes; this device uses the first two components as the texture
  // coordinate, so a scalar is u with v = 0.
  const float u[3] = {0.f, 1.f, 0.5f};
  ANARIArray1D scalarAttribute =
      anariNewArray1D(d, u, nullptr, nullptr, ANARI_FLOAT32, 3);
  anariSetParameter(
      d, scene.geometry, "vertex.attribute0", ANARI_ARRAY1D, &scalarAttribute);
  anariCommitParameters(d, scene.geometry);
  anariRelease(d, scalarAttribute);
  expectTriangleStillRenders(scene, "scalar vertex.attribute0");

  // A normal array of the wrong element type: warned about and ignored, so the
  // triangle keeps its geometric normal and stays lit head-on.
  const float wideNormals[12] = {
      0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  ANARIArray1D badNormals =
      anariNewArray1D(d, wideNormals, nullptr, nullptr, ANARI_FLOAT32_VEC4, 3);
  anariSetParameter(
      d, scene.geometry, "vertex.normal", ANARI_ARRAY1D, &badNormals);
  anariCommitParameters(d, scene.geometry);
  anariRelease(d, badNormals);
  expectTriangleStillRenders(scene, "float4 vertex.normal");

  // A vertex.color array that is too short for the mesh. The material keeps
  // its constant color here, so what is measured is only that the geometry
  // survived.
  const float twoColors[6] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  ANARIArray1D shortColors =
      anariNewArray1D(d, twoColors, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
  anariSetParameter(
      d, scene.geometry, "vertex.color", ANARI_ARRAY1D, &shortColors);
  anariCommitParameters(d, scene.geometry);
  anariRelease(d, shortColors);
  expectTriangleStillRenders(scene, "short vertex.color");

  // ... and a sphere whose per-vertex radii are the wrong type falls back to
  // the constant radius rather than disappearing.
  const float center[3] = {0.f, 0.f, 0.f};
  const float radiusPair[2] = {0.5f, 0.5f};
  ANARIGeometry sphere = anariNewGeometry(d, "sphere");
  ANARIArray1D positions =
      anariNewArray1D(d, center, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
  ANARIArray1D badRadii =
      anariNewArray1D(d, radiusPair, nullptr, nullptr, ANARI_FLOAT32_VEC2, 1);
  anariSetParameter(d, sphere, "vertex.position", ANARI_ARRAY1D, &positions);
  anariSetParameter(d, sphere, "vertex.radius", ANARI_ARRAY1D, &badRadii);
  float radius = 1.f;
  anariSetParameter(d, sphere, "radius", ANARI_FLOAT32, &radius);
  anariCommitParameters(d, sphere);
  anariRelease(d, positions);
  anariRelease(d, badRadii);
  useGeometry(d, scene, sphere);

  auto image = test_scene::renderToFloat(scene);
  const float measured = float(litBounds(image).count) / float(kSize * kSize);
  const float expected = expectedSphereCoverage(radius);
  std::printf("float2 vertex.radius: sphere covers %.4f (expected %.4f)\n",
      measured,
      expected);
  CHECK_MSG(close(measured, expected, 0.02f),
      "an unusable 'vertex.radius' must fall back to the constant 'radius'");

  anariRelease(d, sphere);
  test_scene::release(scene);
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "sphere_geometry";

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

  if (mode == "sphere_geometry")
    testSphereGeometry(device);
  else if (mode == "curve_geometry")
    testCurveGeometry(device);
  else if (mode == "vertex_normals")
    testVertexNormals(device);
  else if (mode == "vertex_colors")
    testVertexColors(device);
  else if (mode == "unusable_attributes")
    testUnusableAttributes(device);
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
