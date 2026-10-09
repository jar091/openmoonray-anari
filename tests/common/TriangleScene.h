// The Milestone 7 reference scene, shared by the ANARI tests that need a real
// render: perspective camera, one triangle, matte material, surface, world,
// one directional light, default renderer, frame.
//
// Everything goes through the public ANARI C API - no internal adapter class
// is linked (master prompt §31.2).
//
// The scene is built so that its expected image can be *derived*: a
// Lambertian surface of albedo a lit head-on by a directional light of
// irradiance E has outgoing radiance L = a * E / pi, so with E = pi the lit
// triangle's color is exactly the material color, and everything the camera
// misses is the renderer's background.

#pragma once

#include <anari/anari.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace test_scene {

constexpr float kPi = 3.14159265358979323846f;

struct TriangleScene
{
  ANARIDevice device{nullptr};

  ANARICamera camera{nullptr};
  ANARIGeometry geometry{nullptr};
  ANARIMaterial material{nullptr};
  ANARISurface surface{nullptr};
  ANARILight light{nullptr};
  ANARIWorld world{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIFrame frame{nullptr};

  uint32_t width{64};
  uint32_t height{64};
};

// Builds the scene. The caller owns every handle in the returned struct and
// releases them with release().
inline TriangleScene build(ANARIDevice d,
    const float albedo[3],
    const float background[4],
    ANARIDataType colorType,
    uint32_t size = 64)
{
  TriangleScene s;
  s.device = d;
  s.width = size;
  s.height = size;

  // --- camera: 3 units in front of the triangle, looking down -Z ----------
  s.camera = anariNewCamera(d, "perspective");
  const float position[3] = {0.f, 0.f, 3.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  anariSetParameter(d, s.camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, s.camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, s.camera, "up", ANARI_FLOAT32_VEC3, up);
  const float fovy = kPi / 3.f;
  const float aspect = 1.f;
  anariSetParameter(d, s.camera, "fovy", ANARI_FLOAT32, &fovy);
  anariSetParameter(d, s.camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(d, s.camera);

  // --- geometry: one triangle in the z = 0 plane, wound counter-clockwise
  //     as seen from the camera, so its normal points at the camera --------
  const float vertices[3][3] = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {0.f, 1.f, 0.f}};
  const uint32_t indices[3] = {0, 1, 2};

  s.geometry = anariNewGeometry(d, "triangle");
  ANARIArray1D positionArray =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIArray1D indexArray =
      anariNewArray1D(d, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 1);
  anariSetParameter(
      d, s.geometry, "vertex.position", ANARI_ARRAY1D, &positionArray);
  anariSetParameter(
      d, s.geometry, "primitive.index", ANARI_ARRAY1D, &indexArray);
  anariCommitParameters(d, s.geometry);
  anariRelease(d, positionArray);
  anariRelease(d, indexArray);

  // --- material -----------------------------------------------------------
  s.material = anariNewMaterial(d, "matte");
  anariSetParameter(d, s.material, "color", ANARI_FLOAT32_VEC3, albedo);
  anariCommitParameters(d, s.material);

  // --- surface ------------------------------------------------------------
  s.surface = anariNewSurface(d);
  anariSetParameter(d, s.surface, "geometry", ANARI_GEOMETRY, &s.geometry);
  anariSetParameter(d, s.surface, "material", ANARI_MATERIAL, &s.material);
  anariCommitParameters(d, s.surface);

  // --- light: shining along -Z, straight onto the triangle's front face ---
  s.light = anariNewLight(d, "directional");
  const float lightColor[3] = {1.f, 1.f, 1.f};
  anariSetParameter(d, s.light, "color", ANARI_FLOAT32_VEC3, lightColor);
  anariSetParameter(d, s.light, "direction", ANARI_FLOAT32_VEC3, direction);
  const float irradiance = kPi;
  anariSetParameter(d, s.light, "irradiance", ANARI_FLOAT32, &irradiance);
  anariCommitParameters(d, s.light);

  // --- world --------------------------------------------------------------
  s.world = anariNewWorld(d);
  ANARIArray1D surfaceArray =
      anariNewArray1D(d, &s.surface, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIArray1D lightArray =
      anariNewArray1D(d, &s.light, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(d, s.world, "surface", ANARI_ARRAY1D, &surfaceArray);
  anariSetParameter(d, s.world, "light", ANARI_ARRAY1D, &lightArray);
  anariCommitParameters(d, s.world);
  anariRelease(d, surfaceArray);
  anariRelease(d, lightArray);

  // --- renderer -----------------------------------------------------------
  s.renderer = anariNewRenderer(d, "default");
  anariSetParameter(
      d, s.renderer, "background", ANARI_FLOAT32_VEC4, background);
  const int pixelSamples = 16;
  anariSetParameter(d, s.renderer, "pixelSamples", ANARI_INT32, &pixelSamples);
  anariCommitParameters(d, s.renderer);

  // --- frame --------------------------------------------------------------
  s.frame = anariNewFrame(d);
  const uint32_t frameSize[2] = {s.width, s.height};
  anariSetParameter(d, s.frame, "size", ANARI_UINT32_VEC2, frameSize);
  anariSetParameter(d, s.frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, s.frame, "renderer", ANARI_RENDERER, &s.renderer);
  anariSetParameter(d, s.frame, "camera", ANARI_CAMERA, &s.camera);
  anariSetParameter(d, s.frame, "world", ANARI_WORLD, &s.world);
  anariCommitParameters(d, s.frame);

  return s;
}

inline void release(TriangleScene &s)
{
  anariRelease(s.device, s.frame);
  anariRelease(s.device, s.renderer);
  anariRelease(s.device, s.world);
  anariRelease(s.device, s.light);
  anariRelease(s.device, s.surface);
  anariRelease(s.device, s.material);
  anariRelease(s.device, s.geometry);
  anariRelease(s.device, s.camera);
}

// Renders and returns the float32 color channel (RGBA per pixel). Empty if
// the channel could not be mapped.
inline std::vector<float> renderToFloat(TriangleScene &s)
{
  anariRenderFrame(s.device, s.frame);
  anariFrameReady(s.device, s.frame, ANARI_WAIT);

  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *pixels = (const float *)anariMapFrame(
      s.device, s.frame, "channel.color", &w, &h, &t);

  std::vector<float> out;
  if (pixels && t == ANARI_FLOAT32_VEC4)
    out.assign(pixels, pixels + size_t(w) * h * 4);
  anariUnmapFrame(s.device, s.frame, "channel.color");
  return out;
}

// The pixel at the center of the frame, which the triangle covers.
inline const float *centerPixel(const std::vector<float> &image, uint32_t size)
{
  return image.data() + 4 * (size_t(size / 2) * size + size / 2);
}


// --- Milestone 8: the same scene with an explicit group and N instances -----
//
// `InstancedScene` keeps the group and the instance handles so a test can
// change a transform and re-render. The camera is pulled back far enough to
// see the whole row of instances.

struct InstancedScene
{
  TriangleScene base;
  ANARIGroup group{nullptr};
  std::vector<ANARIInstance> instances;
};

// One triangle in a group, instanced once per entry in `xOffsets`.
inline InstancedScene buildInstanced(ANARIDevice d,
    const float albedo[3],
    const float background[4],
    const std::vector<float> &xOffsets,
    uint32_t size = 64,
    float cameraDistance = 8.f)
{
  InstancedScene s;
  s.base = build(d, albedo, background, ANARI_FLOAT32_VEC4, size);

  // The scene built above puts the surface straight into the world; here the
  // surface goes into a group instead, and the world holds the instances.
  anariUnsetParameter(d, s.base.world, "surface");

  const float position[3] = {0.f, 0.f, cameraDistance};
  anariSetParameter(d, s.base.camera, "position", ANARI_FLOAT32_VEC3, position);
  anariCommitParameters(d, s.base.camera);

  s.group = anariNewGroup(d);
  ANARIArray1D surfaceArray =
      anariNewArray1D(d, &s.base.surface, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(d, s.group, "surface", ANARI_ARRAY1D, &surfaceArray);
  anariCommitParameters(d, s.group);
  anariRelease(d, surfaceArray);

  for (float x : xOffsets) {
    ANARIInstance instance = anariNewInstance(d, "transform");
    anariSetParameter(d, instance, "group", ANARI_GROUP, &s.group);
    // ANARI mat4 is column-major: the translation is the fourth column.
    const float xfm[16] = {1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        x, 0.f, 0.f, 1.f};
    anariSetParameter(d, instance, "transform", ANARI_FLOAT32_MAT4, xfm);
    anariCommitParameters(d, instance);
    s.instances.push_back(instance);
  }

  ANARIArray1D instanceArray = anariNewArray1D(d,
      s.instances.data(),
      nullptr,
      nullptr,
      ANARI_INSTANCE,
      s.instances.size());
  anariSetParameter(d, s.base.world, "instance", ANARI_ARRAY1D, &instanceArray);
  anariCommitParameters(d, s.base.world);
  anariRelease(d, instanceArray);

  return s;
}

// Moves one instance along x and commits it. Nothing else is touched, which
// is the point: the change has to reach the frame on its own.
inline void setInstanceOffset(
    ANARIDevice d, ANARIInstance instance, float x, float y = 0.f)
{
  const float xfm[16] = {1.f, 0.f, 0.f, 0.f,
      0.f, 1.f, 0.f, 0.f,
      0.f, 0.f, 1.f, 0.f,
      x, y, 0.f, 1.f};
  anariSetParameter(d, instance, "transform", ANARI_FLOAT32_MAT4, xfm);
  anariCommitParameters(d, instance);
}

inline void release(InstancedScene &s)
{
  for (auto instance : s.instances)
    anariRelease(s.base.device, instance);
  s.instances.clear();
  anariRelease(s.base.device, s.group);
  release(s.base);
}

// Pixels the geometry covers, judged by the green channel: the tests use a
// background with no green, so anything green is geometry.
inline unsigned coverage(const std::vector<float> &image, float threshold = 0.05f)
{
  unsigned n = 0;
  for (size_t i = 0; i < image.size() / 4; ++i) {
    if (image[4 * i + 1] > threshold)
      ++n;
  }
  return n;
}

// Mean x of the covered pixels, in pixel units; -1 when nothing is covered.
// This is what makes "the instance moved" a measurement rather than a guess.
inline float coverageCentroidX(
    const std::vector<float> &image, uint32_t width, float threshold = 0.05f)
{
  double sum = 0.0;
  unsigned n = 0;
  for (size_t i = 0; i < image.size() / 4; ++i) {
    if (image[4 * i + 1] > threshold) {
      sum += double(i % width);
      ++n;
    }
  }
  return n ? float(sum / n) : -1.f;
}

} // namespace test_scene
