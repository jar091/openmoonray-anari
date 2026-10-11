// Emitting surfaces sampled by lights: the vendor material parameter
// `emissionSampling` ("front", "back", "both") makes the device put a
// MeshLight on every instance of the surface, and shade the surface with a
// variant of its material that only emits for camera and mirror rays.
//
// The scene is a closed room (4 x 4 x 3, albedo 0.7) lit by nothing but a
// lamp: a square hanging under the ceiling whose front faces down. Such a room is
// what an emitting surface alone renders badly - a path only finds the lamp
// by chance - so the tests measure, through the public ANARI C API,
//
//   * that the lights bring the same light into the room as the surface
//     (nothing is lost, nothing is counted twice), with far less noise;
//   * that each side is one light and the sides add up;
//   * that a textured emission and an `rdla` material work the same way;
//   * that edits reach the lights: the emission, a move, removing and adding
//     the instance, switching the sampling off;
//   * that the number of lights is bounded and what does not fit emits as
//     before, and that an instance the diffuse rays do not see lights
//     nothing;
//   * that a thousand instances of a lamp are one light, and that lamps
//     whose emission reads the space of the object are a light each.
//
// Usage: integration_mesh_lights <noise|sides|reflecting|textured|rdla
//                                 |spaces|updates|limit|visibility|instances
//                                 |churn>

#include "CrashTrace.h"

#include <anari/anari.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
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

constexpr uint32_t kWidth = 128;
constexpr uint32_t kHeight = 96;

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

int intProperty(ANARIDevice d, const char *name)
{
  int n = -1;
  anariGetProperty(d, d, name, ANARI_INT32, &n, sizeof(n), ANARI_WAIT);
  return n;
}

float floatProperty(ANARIDevice d, const char *name)
{
  float f = -1.f;
  anariGetProperty(d, d, name, ANARI_FLOAT32, &f, sizeof(f), ANARI_WAIT);
  return f;
}

template <typename T>
ANARIArray1D array1D(
    ANARIDevice d, ANARIDataType type, const std::vector<T> &values, size_t n)
{
  ANARIArray1D a = anariNewArray1D(d, nullptr, nullptr, nullptr, type, n);
  std::memcpy(anariMapArray(d, a), values.data(), values.size() * sizeof(T));
  anariUnmapArray(d, a);
  return a;
}

ANARIGeometry quads(ANARIDevice d,
    const std::vector<float> &positions,
    const std::vector<float> &uvs = {})
{
  const size_t numVertices = positions.size() / 3;
  std::vector<uint32_t> indices;
  for (uint32_t q = 0; q < numVertices / 4; ++q) {
    const uint32_t v = 4 * q;
    for (uint32_t i : {v, v + 1, v + 2, v, v + 2, v + 3})
      indices.push_back(i);
  }
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  ANARIArray1D p = array1D(d, ANARI_FLOAT32_VEC3, positions, numVertices);
  ANARIArray1D i = array1D(d, ANARI_UINT32_VEC3, indices, indices.size() / 3);
  anariSetParameter(d, geometry, "vertex.position", ANARI_ARRAY1D, &p);
  anariSetParameter(d, geometry, "primitive.index", ANARI_ARRAY1D, &i);
  anariRelease(d, p);
  anariRelease(d, i);
  if (!uvs.empty()) {
    ANARIArray1D t = array1D(d, ANARI_FLOAT32_VEC2, uvs, numVertices);
    anariSetParameter(d, geometry, "vertex.attribute0", ANARI_ARRAY1D, &t);
    anariRelease(d, t);
  }
  anariCommitParameters(d, geometry);
  return geometry;
}

ANARIGroup groupOf(ANARIDevice d, ANARIGeometry geometry, ANARIMaterial material)
{
  ANARISurface surface = anariNewSurface(d);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariSetParameter(d, surface, "material", ANARI_MATERIAL, &material);
  anariCommitParameters(d, surface);
  ANARIGroup group = anariNewGroup(d);
  ANARIArray1D surfaces =
      anariNewArray1D(d, &surface, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(d, group, "surface", ANARI_ARRAY1D, &surfaces);
  anariCommitParameters(d, group);
  anariRelease(d, surfaces);
  anariRelease(d, surface);
  return group;
}

void setTranslation(ANARIDevice d, ANARIInstance instance, float x, float y)
{
  const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, 0, 1};
  anariSetParameter(d, instance, "transform", ANARI_FLOAT32_MAT4, m);
}

// The room, a lamp material and any number of lamps.
struct Room
{
  ANARIDevice device{nullptr};
  ANARICamera camera{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIWorld world{nullptr};
  ANARIFrame frame{nullptr};
  ANARIGroup roomGroup{nullptr};
  ANARIInstance roomInstance{nullptr};
  ANARIMaterial lampMaterial{nullptr};
  ANARIGeometry lampGeometry{nullptr};
  ANARIGroup lampGroup{nullptr};
  std::vector<ANARIInstance> lamps;

  void setSamples(int samples)
  {
    anariSetParameter(device, renderer, "pixelSamples", ANARI_INT32, &samples);
    anariCommitParameters(device, renderer);
  }

  void setSampling(const char *sides)
  {
    anariSetParameter(
        device, lampMaterial, "emissionSampling", ANARI_STRING, sides);
    anariCommitParameters(device, lampMaterial);
  }

  void setEmission(float r, float g, float b)
  {
    const float emissive[3] = {r, g, b};
    anariSetParameter(
        device, lampMaterial, "emissive", ANARI_FLOAT32_VEC3, emissive);
    anariCommitParameters(device, lampMaterial);
  }

  // The lamps which are in the world.
  void setWorld(const std::vector<ANARIInstance> &present)
  {
    std::vector<ANARIInstance> instances = {roomInstance};
    instances.insert(instances.end(), present.begin(), present.end());
    ANARIArray1D array = anariNewArray1D(device,
        instances.data(),
        nullptr,
        nullptr,
        ANARI_INSTANCE,
        instances.size());
    anariSetParameter(device, world, "instance", ANARI_ARRAY1D, &array);
    anariCommitParameters(device, world);
    anariRelease(device, array);
  }

  ANARIInstance addLamp(float x, float y, ANARIGroup group = nullptr)
  {
    ANARIInstance instance = anariNewInstance(device, "transform");
    ANARIGroup g = group ? group : lampGroup;
    anariSetParameter(device, instance, "group", ANARI_GROUP, &g);
    setTranslation(device, instance, x, y);
    anariCommitParameters(device, instance);
    lamps.push_back(instance);
    return instance;
  }

  // Everything the room holds, and what a test made for it.
  void release(std::initializer_list<ANARIObject> more = {})
  {
    for (ANARIObject object : more)
      anariRelease(device, object);
    for (ANARIInstance lamp : lamps)
      anariRelease(device, lamp);
    lamps.clear();
    for (ANARIObject object : std::initializer_list<ANARIObject>{frame,
             world,
             renderer,
             camera,
             roomInstance,
             roomGroup,
             lampGroup,
             lampGeometry,
             lampMaterial})
      anariRelease(device, object);
  }

  std::vector<float> render()
  {
    anariRenderFrame(device, frame);
    anariFrameReady(device, frame, ANARI_WAIT);
    uint32_t w = 0, h = 0;
    ANARIDataType t = ANARI_UNKNOWN;
    const auto *pixels = (const float *)anariMapFrame(
        device, frame, "channel.color", &w, &h, &t);
    std::vector<float> out;
    if (pixels && t == ANARI_FLOAT32_VEC4)
      out.assign(pixels, pixels + size_t(w) * h * 4);
    anariUnmapFrame(device, frame, "channel.color");
    CHECK_MSG(out.size() == size_t(kWidth) * kHeight * 4, "no frame");
    return out;
  }
};

// The lamp: a square of half size `half` which hangs under the ceiling, front
// (the side its normal points to) down.
std::vector<float> lampPositions(float half, float z = 2.2f)
{
  return {-half, -half, z, -half, half, z, half, half, z, half, -half, z};
}

Room buildRoom(ANARIDevice d, float lampHalf, float emission, bool addOneLamp)
{
  Room r;
  r.device = d;

  r.camera = anariNewCamera(d, "perspective");
  const float position[3] = {0.f, -1.9f, 1.5f};
  const float direction[3] = {0.f, 1.f, 0.f};
  const float up[3] = {0.f, 0.f, 1.f};
  const float fovy = 1.4f;
  const float aspect = float(kWidth) / float(kHeight);
  anariSetParameter(d, r.camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, r.camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, r.camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(d, r.camera, "fovy", ANARI_FLOAT32, &fovy);
  anariSetParameter(d, r.camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(d, r.camera);

  const float X = 2.f, Y = 2.f, Z = 3.f;
  ANARIGeometry roomGeometry = quads(d,
      {-X, -Y, 0, X, -Y, 0, X, Y, 0, -X, Y, 0, // floor
          -X, -Y, Z, -X, Y, Z, X, Y, Z, X, -Y, Z, // ceiling
          -X, Y, 0, X, Y, 0, X, Y, Z, -X, Y, Z, // far wall
          -X, -Y, 0, -X, -Y, Z, X, -Y, Z, X, -Y, 0, // wall behind the camera
          -X, -Y, 0, -X, Y, 0, -X, Y, Z, -X, -Y, Z, // left
          X, -Y, 0, X, -Y, Z, X, Y, Z, X, Y, 0}); // right
  ANARIMaterial wall = anariNewMaterial(d, "matte");
  const float albedo[3] = {0.7f, 0.7f, 0.7f};
  anariSetParameter(d, wall, "color", ANARI_FLOAT32_VEC3, albedo);
  anariCommitParameters(d, wall);
  r.roomGroup = groupOf(d, roomGeometry, wall);
  anariRelease(d, wall);
  anariRelease(d, roomGeometry);
  r.roomInstance = anariNewInstance(d, "transform");
  anariSetParameter(d, r.roomInstance, "group", ANARI_GROUP, &r.roomGroup);
  anariCommitParameters(d, r.roomInstance);

  r.lampMaterial = anariNewMaterial(d, "physicallyBased");
  const float black[3] = {0.f, 0.f, 0.f};
  const float one = 1.f, zero = 0.f;
  anariSetParameter(d, r.lampMaterial, "baseColor", ANARI_FLOAT32_VEC3, black);
  anariSetParameter(d, r.lampMaterial, "roughness", ANARI_FLOAT32, &one);
  anariSetParameter(d, r.lampMaterial, "metallic", ANARI_FLOAT32, &zero);
  anariCommitParameters(d, r.lampMaterial);
  r.setEmission(emission, emission, emission);
  r.lampGeometry = quads(d, lampPositions(lampHalf), {0, 0, 0, 1, 1, 1, 1, 0});
  r.lampGroup = groupOf(d, r.lampGeometry, r.lampMaterial);

  r.renderer = anariNewRenderer(d, "default");
  const float background[4] = {0.f, 0.f, 0.f, 1.f};
  const int bounces = 8;
  const float noClamp = 0.f;
  anariSetParameter(d, r.renderer, "background", ANARI_FLOAT32_VEC4, background);
  anariSetParameter(d, r.renderer, "maxBounce", ANARI_INT32, &bounces);
  anariSetParameter(d, r.renderer, "maxDiffuseBounce", ANARI_INT32, &bounces);
  anariSetParameter(d, r.renderer, "clampIndirect", ANARI_FLOAT32, &noClamp);
  anariCommitParameters(d, r.renderer);
  r.setSamples(16);

  r.world = anariNewWorld(d);
  if (addOneLamp)
    r.addLamp(0.f, 0.f);
  r.setWorld(r.lamps);

  r.frame = anariNewFrame(d);
  const uint32_t size[2] = {kWidth, kHeight};
  const ANARIDataType colorType = ANARI_FLOAT32_VEC4;
  anariSetParameter(d, r.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, r.frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, r.frame, "renderer", ANARI_RENDERER, &r.renderer);
  anariSetParameter(d, r.frame, "camera", ANARI_CAMERA, &r.camera);
  anariSetParameter(d, r.frame, "world", ANARI_WORLD, &r.world);
  anariCommitParameters(d, r.frame);
  return r;
}

// The lower part of the frame: the floor and the foot of the walls, which
// only the lamp lights. Columns from `x0` to `x1` of the width.
struct Region
{
  float mean[3]{0.f, 0.f, 0.f};
  float luminance{0.f};
};

Region lowerPart(const std::vector<float> &image, float x0 = 0.f, float x1 = 1.f)
{
  Region region;
  if (image.empty())
    return region;
  size_t count = 0;
  for (uint32_t y = 0; y < kHeight * 45 / 100; ++y) {
    for (uint32_t x = uint32_t(x0 * kWidth); x < uint32_t(x1 * kWidth); ++x) {
      const float *p = image.data() + 4 * (size_t(y) * kWidth + x);
      for (int c = 0; c < 3; ++c)
        region.mean[c] += p[c];
      ++count;
    }
  }
  for (int c = 0; c < 3; ++c)
    region.mean[c] /= float(count);
  region.luminance = (region.mean[0] + region.mean[1] + region.mean[2]) / 3.f;
  return region;
}

// Root mean square difference to a reference in the lower part, relative to
// the mean of the reference.
float noise(const std::vector<float> &image, const std::vector<float> &reference)
{
  if (image.empty() || reference.empty())
    return 0.f;
  double sum = 0.0, mean = 0.0;
  size_t count = 0;
  for (uint32_t y = 0; y < kHeight * 45 / 100; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const size_t i = 4 * (size_t(y) * kWidth + x);
      const double a = (image[i] + image[i + 1] + image[i + 2]) / 3.0;
      const double b = (reference[i] + reference[i + 1] + reference[i + 2]) / 3.0;
      sum += (a - b) * (a - b);
      mean += b;
      ++count;
    }
  }
  mean /= double(count);
  return mean > 0.0 ? float(std::sqrt(sum / double(count)) / mean) : 0.f;
}

bool within(float a, float b, float tolerance)
{
  return std::fabs(a - b) <= tolerance * std::fabs(b);
}

double seconds()
{
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// --- anari.mesh_lights_noise --------------------------------------------------

void testNoise(ANARIDevice d)
{
  for (const float half : {0.5f, 0.1f}) {
    // The same power from both lamps.
    Room room = buildRoom(d, half, 20.f / (4.f * half * half), true);

    room.setSampling("both");
    room.setSamples(400);
    const auto reference = room.render();
    CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
        "a lamp sampled on both sides is one light");

    room.setSamples(16);
    double start = seconds();
    const auto sampled = room.render();
    const double sampledSeconds = seconds() - start;

    room.setSampling("none");
    start = seconds();
    const auto surface = room.render();
    const double surfaceSeconds = seconds() - start;
    CHECK_MSG(intProperty(d, "moonray.meshLights") == 0,
        "no lights without emissionSampling");

    room.setSamples(400);
    const auto surfaceReference = room.render();

    const float referenceMean = lowerPart(reference).luminance;
    const float surfaceMean = lowerPart(surfaceReference).luminance;
    const float sampledNoise = noise(sampled, reference);
    const float surfaceNoise = noise(surface, reference);
    std::printf("lamp of %.1f m: mean %.4f with lights, %.4f as a surface "
                "(400 samples); relative RMS at 16 samples %.3f with lights, "
                "%.3f as a surface; %.2f s and %.2f s\n",
        2.f * half,
        referenceMean,
        surfaceMean,
        sampledNoise,
        surfaceNoise,
        sampledSeconds,
        surfaceSeconds);
    CHECK_MSG(referenceMean > 0.05f, "the room is lit");
    // The same light: a difference would be light lost or counted twice.
    CHECK_MSG(within(referenceMean, surfaceMean, half > 0.3f ? 0.06f : 0.25f),
        "the lights bring the light of the surface into the room");
    CHECK_MSG(sampledNoise < 0.35f, "the room has little noise with lights");
    CHECK_MSG(surfaceNoise > 3.f * sampledNoise,
        "the lights remove most of the noise");
    room.release();
  }
}

// --- anari.mesh_lights_sides ---------------------------------------------------

void testSides(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, true);
  room.setSamples(144);

  room.setSampling("front");
  const float front = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "one side, one light");
  room.setSampling("back");
  const float back = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "one side, one light");
  room.setSampling("both");
  const float both = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "two sides, still one light: both are faces of its mesh");

  std::printf("front %.4f, back %.4f, both %.4f\n", front, back, both);
  // The front of the lamp faces the floor; its back lights the ceiling, and
  // the floor only gets what the ceiling reflects.
  CHECK_MSG(front > 1.5f * back, "the front side is the one that faces down");
  CHECK_MSG(back > 0.f, "the back side emits too");
  CHECK_MSG(within(both, front + back, 0.05f), "the two sides add up");
  room.release();
}

// --- anari.mesh_lights_reflecting ----------------------------------------------

// A lamp which also reflects: a white diffuse surface that emits. A ray
// which leaves such a surface must not find the light of the surface itself
// at distance zero, or the lamp would reflect its own emission.
void testReflecting(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, true);
  const float white[3] = {0.9f, 0.9f, 0.9f};
  anariSetParameter(
      d, room.lampMaterial, "baseColor", ANARI_FLOAT32_VEC3, white);
  anariCommitParameters(d, room.lampMaterial);

  room.setSamples(256);
  room.setSampling("both");
  const float sampled = lowerPart(room.render()).luminance;
  room.setSampling("front");
  const float front = lowerPart(room.render()).luminance;
  room.setSampling("none");
  room.setSamples(1024);
  const float surface = lowerPart(room.render()).luminance;
  std::printf("both sides %.4f, front %.4f, as a surface %.4f\n",
      sampled, front, surface);
  CHECK_MSG(within(sampled, surface, 0.05f),
      "a lamp which reflects does not reflect its own lights");
  room.release();
}

// --- anari.mesh_lights_textured ------------------------------------------------

void testTextured(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, true);

  // Left half red, right half blue, with u along x.
  const std::vector<float> pixels = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
  ANARIArray2D image = anariNewArray2D(
      d, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2, 1);
  std::memcpy(anariMapArray(d, image), pixels.data(), pixels.size() * sizeof(float));
  anariUnmapArray(d, image);
  ANARISampler sampler = anariNewSampler(d, "image2D");
  anariSetParameter(d, sampler, "image", ANARI_ARRAY2D, &image);
  anariSetParameter(d, sampler, "filter", ANARI_STRING, "nearest");
  // 40 times the image: the emission is the sampler's value.
  const float scale[16] = {40, 0, 0, 0, 0, 40, 0, 0, 0, 0, 40, 0, 0, 0, 0, 1};
  anariSetParameter(d, sampler, "outTransform", ANARI_FLOAT32_MAT4, scale);
  anariCommitParameters(d, sampler);
  anariSetParameter(d, room.lampMaterial, "emissive", ANARI_SAMPLER, &sampler);
  anariCommitParameters(d, room.lampMaterial);
  anariRelease(d, sampler);
  anariRelease(d, image);

  room.setSamples(256);
  room.setSampling("both");
  const auto sampled = room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "the lamp is sampled");
  room.setSampling("none");
  room.setSamples(1024);
  const auto surface = room.render();

  const Region sampledAll = lowerPart(sampled), surfaceAll = lowerPart(surface);
  const Region sampledLeft = lowerPart(sampled, 0.f, 0.3f);
  const Region sampledRight = lowerPart(sampled, 0.7f, 1.f);
  std::printf("with lights %.4f %.4f %.4f, as a surface %.4f %.4f %.4f; left "
              "red/blue %.2f, right red/blue %.2f\n",
      sampledAll.mean[0], sampledAll.mean[1], sampledAll.mean[2],
      surfaceAll.mean[0], surfaceAll.mean[1], surfaceAll.mean[2],
      sampledLeft.mean[0] / sampledLeft.mean[2],
      sampledRight.mean[0] / sampledRight.mean[2]);
  CHECK_MSG(sampledAll.mean[0] > 0.02f && sampledAll.mean[2] > 0.02f,
      "both colors of the image light the room");
  CHECK_MSG(within(sampledAll.mean[0], surfaceAll.mean[0], 0.1f)
          && within(sampledAll.mean[2], surfaceAll.mean[2], 0.1f),
      "the lights emit the image the surface emits");
  CHECK_MSG(sampledAll.mean[1] < 0.02f * sampledAll.mean[0],
      "the image has no green");
  // The red half is on one side of the room and the blue one on the other.
  const float left = sampledLeft.mean[0] / sampledLeft.mean[2];
  const float right = sampledRight.mean[0] / sampledRight.mean[2];
  CHECK_MSG(left > 1.1f * right || right > 1.1f * left,
      "the light keeps where the image is red and where it is blue");
  room.release();
}

// --- anari.mesh_lights_rdla ------------------------------------------------------

void testRdla(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, false);

  // What an exporter writes: the material, and the root which the surface
  // is shaded with while lights sample its emission.
  const char *source = R"(
DwaBaseMaterial("/lamp/full") {
    ["specular"] = 0,
    ["albedo"] = Rgb(0, 0, 0),
    ["show_emission"] = true,
    ["emission"] = Rgb(20, 10, 5),
}
DwaBaseMaterial("/lamp/dark") {
    ["specular"] = 0,
    ["albedo"] = Rgb(0, 0, 0),
}
RaySwitchMaterial("/lamp/sampled") {
    ["default_material"] = DwaBaseMaterial("/lamp/dark"),
    ["camera_ray_material"] = DwaBaseMaterial("/lamp/full"),
    ["indirect_mirror_ray_material"] = DwaBaseMaterial("/lamp/full"),
    ["indirect_diffuse_ray_material"] = DwaBaseMaterial("/lamp/dark"),
    ["indirect_glossy_ray_material"] = DwaBaseMaterial("/lamp/dark"),
}
)";
  ANARIMaterial material = anariNewMaterial(d, "rdla");
  const float color[3] = {20.f, 10.f, 5.f};
  anariSetParameter(d, material, "source", ANARI_STRING, source);
  anariSetParameter(d, material, "materialName", ANARI_STRING, "/lamp/full");
  anariSetParameter(
      d, material, "sampledMaterialName", ANARI_STRING, "/lamp/sampled");
  anariSetParameter(d, material, "emissionColor", ANARI_FLOAT32_VEC3, color);
  anariSetParameter(d, material, "emissionSampling", ANARI_STRING, "both");
  anariCommitParameters(d, material);
  ANARIGroup group = groupOf(d, room.lampGeometry, material);
  room.addLamp(0.f, 0.f, group);
  room.setWorld(room.lamps);

  room.setSamples(256);
  const auto sampled = room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "the lamp is sampled");
  const int loads = intProperty(d, "moonray.fragmentLoads");

  anariSetParameter(d, material, "emissionSampling", ANARI_STRING, "none");
  anariCommitParameters(d, material);
  room.setSamples(1024);
  const auto surface = room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 0, "not sampled any more");
  CHECK_MSG(intProperty(d, "moonray.fragmentLoads") == loads,
      "the fragment is not loaded again for another sampling");

  const Region a = lowerPart(sampled), b = lowerPart(surface);
  std::printf("with lights %.4f %.4f %.4f, as a surface %.4f %.4f %.4f\n",
      a.mean[0], a.mean[1], a.mean[2], b.mean[0], b.mean[1], b.mean[2]);
  for (int c = 0; c < 3; ++c)
    CHECK_MSG(within(a.mean[c], b.mean[c], 0.08f), "the same light, per channel");

  // A name the fragment does not have: a warning, and the surface emits.
  anariSetParameter(d, material, "emissionSampling", ANARI_STRING, "both");
  anariSetParameter(d, material, "sampledMaterialName", ANARI_STRING, "/lamp/x");
  anariCommitParameters(d, material);
  room.setSamples(256);
  const Region fallback = lowerPart(room.render());
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 0,
      "no lights without the variant of the material");
  CHECK_MSG(within(fallback.mean[0], b.mean[0], 0.2f), "the surface still emits");
  room.release({material, group});
}

// --- anari.mesh_lights_spaces ----------------------------------------------------

// An emission which fades out from the middle of the lamp, by the position
// in the space of the object (the lamp hangs at 2.2 in its own space; the
// second transform is what an instanced geometry needs to get there). Two
// instances of such a lamp cannot share one mesh: there the map would find
// one space for both.
void testSpaces(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, false);
  const char *source = R"(
AttributeMap("/fade/P") {
    ["map_type"] = 1,
}
TransformSpaceMap("/fade/object") {
    ["input"] = bind(AttributeMap("/fade/P"), Vec3(1, 1, 1)),
    ["input_type"] = 0,
    ["from_space"] = 0,
    ["to_space"] = 4,
}
TransformSpaceMap("/fade/instance") {
    ["input"] = bind(TransformSpaceMap("/fade/object"), Vec3(1, 1, 1)),
    ["input_type"] = 0,
    ["from_space"] = 2,
    ["to_space"] = 7,
}
OpMap("/fade/centered") {
    ["operation"] = 1,
    ["op1"] = bind(TransformSpaceMap("/fade/instance"), Rgb(1, 1, 1)),
    ["op2"] = Rgb(0, 0, 2.2),
}
OpMap("/fade/length") {
    ["operation"] = 20,
    ["op1"] = bind(OpMap("/fade/centered"), Rgb(1, 1, 1)),
}
OpMap("/fade/one_minus") {
    ["operation"] = 1,
    ["op1"] = Rgb(1, 1, 1),
    ["op2"] = bind(OpMap("/fade/length"), Rgb(1, 1, 1)),
}
ClampMap("/fade/map") {
    ["input"] = bind(OpMap("/fade/one_minus"), Rgb(1, 1, 1)),
    ["clamp_min"] = 0,
    ["clamp_max"] = 1,
}
DwaBaseMaterial("/fade/full") {
    ["specular"] = 0,
    ["albedo"] = Rgb(0, 0, 0),
    ["show_emission"] = true,
    ["emission"] = bind(ClampMap("/fade/map"), Rgb(30, 30, 30)),
}
DwaBaseMaterial("/fade/dark") {
    ["specular"] = 0,
    ["albedo"] = Rgb(0, 0, 0),
}
RaySwitchMaterial("/fade/sampled") {
    ["default_material"] = DwaBaseMaterial("/fade/dark"),
    ["camera_ray_material"] = DwaBaseMaterial("/fade/full"),
    ["indirect_mirror_ray_material"] = DwaBaseMaterial("/fade/full"),
    ["indirect_diffuse_ray_material"] = DwaBaseMaterial("/fade/dark"),
    ["indirect_glossy_ray_material"] = DwaBaseMaterial("/fade/dark"),
}
)";
  ANARIMaterial material = anariNewMaterial(d, "rdla");
  const float color[3] = {30.f, 30.f, 30.f};
  anariSetParameter(d, material, "source", ANARI_STRING, source);
  anariSetParameter(d, material, "materialName", ANARI_STRING, "/fade/full");
  anariSetParameter(
      d, material, "sampledMaterialName", ANARI_STRING, "/fade/sampled");
  anariSetParameter(d, material, "emissionColor", ANARI_FLOAT32_VEC3, color);
  anariSetParameter(d, material, "emissionMap", ANARI_STRING, "/fade/map");
  anariSetParameter(d, material, "emissionSampling", ANARI_STRING, "both");
  anariCommitParameters(d, material);
  ANARIGroup group = groupOf(d, room.lampGeometry, material);
  room.addLamp(-1.f, 0.f, group);
  room.addLamp(1.f, 0.f, group);
  room.setWorld(room.lamps);

  room.setSamples(16);
  const auto sampled = room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 2,
      "a light for each instance: the map reads the space of the object");
  room.setSamples(400);
  const auto reference = room.render();

  anariSetParameter(d, material, "emissionSampling", ANARI_STRING, "none");
  anariCommitParameters(d, material);
  room.setSamples(16);
  const auto surface = room.render();
  room.setSamples(1024);
  const auto surfaceReference = room.render();

  const float sampledMean = lowerPart(reference).luminance;
  const float surfaceMean = lowerPart(surfaceReference).luminance;
  const float sampledNoise = noise(sampled, reference);
  const float surfaceNoise = noise(surface, reference);
  std::printf("mean %.4f with lights, %.4f as surfaces; relative RMS at 16 "
              "samples %.3f with lights, %.3f as surfaces\n",
      sampledMean, surfaceMean, sampledNoise, surfaceNoise);
  CHECK_MSG(surfaceMean > 0.05f, "the room is lit");
  CHECK_MSG(within(sampledMean, surfaceMean, 0.06f),
      "the lights emit what the surfaces emit, each in the space of its lamp");
  // Faces which the map leaves dark, or which face away, are not sampled:
  // the light knows where the map is bright.
  CHECK_MSG(sampledNoise < 0.2f, "little noise with lights");
  CHECK_MSG(surfaceNoise > 3.f * sampledNoise, "the lights remove the noise");

  // Without the map the two lamps are one light.
  anariSetParameter(d, material, "emissionSampling", ANARI_STRING, "both");
  anariUnsetParameter(d, material, "emissionMap");
  anariCommitParameters(d, material);
  room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "one light for both instances without a map which tells them apart");

  // An emission which reads the direction of view: no light has one, so the
  // surface keeps its emission for all rays.
  const char *viewSource = R"(
AttributeMap("/view/wo") {
    ["map_type"] = 20,
}
OpMap("/view/length") {
    ["operation"] = 20,
    ["op1"] = bind(AttributeMap("/view/wo"), Rgb(1, 1, 1)),
}
DwaBaseMaterial("/view/full") {
    ["specular"] = 0,
    ["albedo"] = Rgb(0, 0, 0),
    ["show_emission"] = true,
    ["emission"] = bind(OpMap("/view/length"), Rgb(30, 30, 30)),
}
DwaBaseMaterial("/view/dark") {
    ["specular"] = 0,
    ["albedo"] = Rgb(0, 0, 0),
}
RaySwitchMaterial("/view/sampled") {
    ["default_material"] = DwaBaseMaterial("/view/dark"),
    ["camera_ray_material"] = DwaBaseMaterial("/view/full"),
    ["indirect_mirror_ray_material"] = DwaBaseMaterial("/view/full"),
    ["indirect_diffuse_ray_material"] = DwaBaseMaterial("/view/dark"),
    ["indirect_glossy_ray_material"] = DwaBaseMaterial("/view/dark"),
}
)";
  anariSetParameter(d, material, "source", ANARI_STRING, viewSource);
  anariSetParameter(d, material, "materialName", ANARI_STRING, "/view/full");
  anariSetParameter(
      d, material, "sampledMaterialName", ANARI_STRING, "/view/sampled");
  anariSetParameter(d, material, "emissionMap", ANARI_STRING, "/view/length");
  anariCommitParameters(d, material);
  room.setSamples(256);
  const float viewed = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 0,
      "no light for an emission which reads the direction of view");
  CHECK_MSG(viewed > 0.05f, "the surface lights the room itself");
  std::printf("emission by the direction of view: %.4f, no light\n", viewed);
  room.release({material, group});
}

// --- anari.mesh_lights_updates ---------------------------------------------------

void testUpdates(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, true);
  room.setSampling("both");
  room.setSamples(64);
  ANARIInstance lamp = room.lamps.front();

  const auto report = [&](const char *what) {
    std::printf("%-28s %d objects written, sync %.2f ms, %d lights\n",
        what,
        intProperty(d, "moonray.objectsUpdatedLastSync"),
        floatProperty(d, "moonray.syncMillisecondsLastFrame"),
        intProperty(d, "moonray.meshLights"));
  };

  const float first = lowerPart(room.render()).luminance;
  report("first frame");
  CHECK_MSG(first > 0.05f, "the room is lit");
  CHECK_MSG(within(lowerPart(room.render()).luminance, first, 0.05f),
      "an unchanged scene renders the same");
  report("nothing changed");
  CHECK_MSG(intProperty(d, "moonray.objectsUpdatedLastSync") == 0,
      "nothing is written for an unchanged scene");

  // The material: twice the emission.
  room.setEmission(40.f, 40.f, 40.f);
  const float brighter = lowerPart(room.render()).luminance;
  report("emission doubled");
  CHECK_MSG(within(brighter, 2.f * first, 0.08f), "the lights follow the material");

  // The object: to the left wall. The left of the floor gets brighter than
  // the right.
  setTranslation(d, lamp, -1.4f, 0.f);
  anariCommitParameters(d, lamp);
  const auto moved = room.render();
  report("lamp moved");
  const float left = lowerPart(moved, 0.f, 0.3f).luminance;
  const float right = lowerPart(moved, 0.7f, 1.f).luminance;
  std::printf("moved: left %.4f, right %.4f\n", left, right);
  CHECK_MSG(left > 1.3f * right, "the lights follow the object");
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "still one light");
  setTranslation(d, lamp, 0.f, 0.f);
  anariCommitParameters(d, lamp);

  // Deleted, and back, a few times: the lights go and come with it.
  for (int round = 0; round < 3; ++round) {
    room.setWorld({});
    const float dark = lowerPart(room.render()).luminance;
    if (round == 0)
      report("lamp removed");
    CHECK_MSG(intProperty(d, "moonray.meshLights") == 0, "no lamp, no lights");
    CHECK_MSG(dark < 0.01f * brighter, "no lamp, no light in the room");
    room.setWorld(room.lamps);
    const float back = lowerPart(room.render()).luminance;
    if (round == 0)
      report("lamp back");
    CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "the light is back");
    CHECK_MSG(within(back, brighter, 0.08f), "as bright as before");
  }

  // A new instance object for the same lamp, as an application does which
  // builds its instances again after an edit.
  anariRelease(d, lamp);
  room.lamps.clear();
  room.addLamp(0.f, 0.f);
  room.setWorld(room.lamps);
  const float replaced = lowerPart(room.render()).luminance;
  report("instance replaced");
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1, "one light, not two");
  CHECK_MSG(within(replaced, brighter, 0.08f), "as bright as before");

  // Not sampled any more: the surface emits for all rays again.
  room.setSampling("none");
  room.setSamples(400);
  const float surface = lowerPart(room.render()).luminance;
  report("sampling switched off");
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 0, "no lights");
  CHECK_MSG(within(surface, brighter, 0.1f), "the surface lights the room itself");
  room.release();
}

// --- anari.mesh_lights_limit -----------------------------------------------------

void testLimit(ANARIDevice d)
{
  Room room = buildRoom(d, 0.15f, 60.f, false);
  // Five instances of one small lamp, and one large lamp of its own.
  for (int i = 0; i < 5; ++i)
    room.addLamp(-1.2f + 0.6f * float(i), 1.f);
  ANARIMaterial largeMaterial = anariNewMaterial(d, "physicallyBased");
  const float black[3] = {0.f, 0.f, 0.f}, emissive[3] = {10.f, 10.f, 10.f};
  anariSetParameter(d, largeMaterial, "baseColor", ANARI_FLOAT32_VEC3, black);
  anariSetParameter(d, largeMaterial, "emissive", ANARI_FLOAT32_VEC3, emissive);
  anariSetParameter(d, largeMaterial, "emissionSampling", ANARI_STRING, "both");
  anariCommitParameters(d, largeMaterial);
  ANARIGeometry largeGeometry = quads(d, lampPositions(0.5f, 2.6f));
  ANARIGroup largeGroup = groupOf(d, largeGeometry, largeMaterial);
  room.addLamp(0.f, -0.8f, largeGroup);
  room.setWorld(room.lamps);
  room.setSampling("both");
  room.setSamples(256);

  const auto setLimit = [&](int limit) {
    anariSetParameter(
        d, room.renderer, "moonray.maxMeshLights", ANARI_INT32, &limit);
    anariCommitParameters(d, room.renderer);
  };

  // The five instances of the small lamp are the faces of one light.
  const float all = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 2,
      "one light for the small lamps and one for the large one");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 0, "all fit");

  // Room for one: the small lamps have more power together.
  setLimit(1);
  const float some = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "the emitter with the most power gets the light");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 1,
      "the light of the large lamp does not fit");

  // An instance which other rays see cannot be in the same light: MoonRay
  // has the visibility on the light. The small lamps now take two lights,
  // which do not fit, and they get both or none.
  const int8_t no = 0;
  anariSetParameter(d, room.lamps[0], "visibility.glossy", ANARI_BOOL, &no);
  anariCommitParameters(d, room.lamps[0]);
  const float other = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "the large lamp gets the light now");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 2,
      "the two lights of the small lamps do not fit");
  setLimit(8);
  const float three = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 3,
      "a light for each set of rays which see the small lamps");

  // The triangles of the lights are bounded too: the five small lamps are
  // twenty with both sides, the large one four.
  const int fewTriangles = 10;
  anariSetParameter(d, room.renderer, "moonray.maxMeshLightTriangles",
      ANARI_INT32, &fewTriangles);
  anariCommitParameters(d, room.renderer);
  room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "only the large lamp fits into ten triangles");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 2,
      "the lights of the small lamps would have more");
  anariUnsetParameter(d, room.renderer, "moonray.maxMeshLightTriangles");
  // An emitter with a small share of the power of all of them gets no light:
  // the small lamps have 73 % of it together, the large one 27 %.
  const float half = 0.5f;
  anariSetParameter(d, room.renderer, "moonray.minMeshLightPowerShare",
      ANARI_FLOAT32, &half);
  anariCommitParameters(d, room.renderer);
  room.render();
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 2,
      "only the small lamps have half of the power");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 0,
      "the large lamp is not over a limit, it is not worth a light");
  anariUnsetParameter(d, room.renderer, "moonray.minMeshLightPowerShare");
  anariCommitParameters(d, room.renderer);

  setLimit(0);
  room.setSamples(1024);
  const float none = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 0, "no lights at all");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 3, "none fits");

  std::printf("all lights %.4f, small lamps only %.4f, large lamp only %.4f, "
              "three lights %.4f, none %.4f\n",
      all, some, other, three, none);
  CHECK_MSG(within(all, none, 0.1f) && within(some, none, 0.1f)
          && within(other, none, 0.1f) && within(three, none, 0.1f),
      "the same light however many of the lamps are lights");
  room.release({largeGroup, largeGeometry, largeMaterial});
}

// --- anari.mesh_lights_visibility ------------------------------------------------

void testVisibility(ANARIDevice d)
{
  Room room = buildRoom(d, 0.5f, 20.f, true);
  room.setSampling("both");
  room.setSamples(64);
  ANARIInstance lamp = room.lamps.front();
  const float lit = lowerPart(room.render()).luminance;

  // The walls are diffuse: a lamp the diffuse rays do not see leaves the
  // room dark, as a surface and as a light.
  const int8_t no = 0;
  anariSetParameter(d, lamp, "visibility.diffuse", ANARI_BOOL, &no);
  anariCommitParameters(d, lamp);
  const float hiddenFromDiffuse = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "still a light: the glossy rays see it");
  CHECK_MSG(hiddenFromDiffuse < 0.02f * lit,
      "hidden from diffuse rays, the lamp does not light the walls");

  for (const char *flag : {"visibility.glossy",
           "visibility.transmission",
           "visibility.volumeScatter"})
    anariSetParameter(d, lamp, flag, ANARI_BOOL, &no);
  anariCommitParameters(d, lamp);
  const float hidden = lowerPart(room.render()).luminance;
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 0,
      "an instance only the camera sees gets no lights");
  CHECK_MSG(hidden < 0.02f * lit, "and lights nothing");
  std::printf("lit %.4f, hidden from diffuse rays %.5f, camera only %.5f\n",
      lit, hiddenFromDiffuse, hidden);
  room.release();
}

// --- anari.mesh_lights_instances -------------------------------------------------

// A thousand instances of one small lamp are one light: MoonRay samples every
// light at every hit, a thousand lights would be a thousand shadow rays.
void testInstances(ANARIDevice d)
{
  // 20 W as in the other rooms: 1000 lamps of 4 x 4 cm.
  Room room = buildRoom(d, 0.02f, 20.f / (1000.f * 0.04f * 0.04f), false);
  for (int y = 0; y < 25; ++y) {
    for (int x = 0; x < 40; ++x)
      room.addLamp(-1.755f + 0.09f * float(x), -1.08f + 0.09f * float(y));
  }
  room.setWorld(room.lamps);
  room.setSampling("both");
  room.setSamples(16);

  double start = seconds();
  const auto sampled = room.render();
  const double sampledSeconds = seconds() - start;
  const int written = intProperty(d, "moonray.objectsUpdatedLastSync");
  const float syncMilliseconds =
      floatProperty(d, "moonray.syncMillisecondsLastFrame");
  CHECK_MSG(intProperty(d, "moonray.meshLights") == 1,
      "a thousand instances of a lamp are one light");
  CHECK_MSG(intProperty(d, "moonray.meshLightsOverLimit") == 0, "which fits");
  room.setSamples(400);
  const auto reference = room.render();

  // One of them moves: the light is built again, and only the light.
  setTranslation(d, room.lamps[0], -1.755f, -1.2f);
  anariCommitParameters(d, room.lamps[0]);
  room.setSamples(16);
  room.render();
  const int movedWritten = intProperty(d, "moonray.objectsUpdatedLastSync");
  const float movedMilliseconds =
      floatProperty(d, "moonray.syncMillisecondsLastFrame");

  room.setSampling("none");
  start = seconds();
  const auto surface = room.render();
  const double surfaceSeconds = seconds() - start;
  room.setSamples(400);
  const auto surfaceReference = room.render();

  const float sampledMean = lowerPart(reference).luminance;
  const float surfaceMean = lowerPart(surfaceReference).luminance;
  const float sampledNoise = noise(sampled, reference);
  const float surfaceNoise = noise(surface, reference);
  std::printf("1000 lamps: mean %.4f with the light, %.4f as surfaces (400 "
              "samples); relative RMS at 16 samples %.3f and %.3f; %.2f s and "
              "%.2f s; first sync %d objects in %.1f ms, one lamp moved %d "
              "objects in %.1f ms\n",
      sampledMean, surfaceMean, sampledNoise, surfaceNoise, sampledSeconds,
      surfaceSeconds, written, syncMilliseconds, movedWritten,
      movedMilliseconds);
  CHECK_MSG(within(sampledMean, surfaceMean, 0.1f),
      "the light brings the light of the surfaces into the room");
  CHECK_MSG(surfaceNoise > 2.f * sampledNoise,
      "the light removes most of the noise");
  room.release();
}

// --- editcost (a measurement, not a test) ----------------------------------------

// The time of a frame of one sample after an edit of the lamp, as a surface
// and with its light, for a lamp of two triangles and for one of many: what
// the light adds is MoonRay building the mesh of the light again.
void measureEditCost(ANARIDevice d)
{
  for (const int n : {1, 256}) {
    Room room = buildRoom(d, 0.5f, 20.f, false);
    std::vector<float> positions;
    const float step = 1.f / float(n);
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        const float x0 = -0.5f + step * float(x), y0 = -0.5f + step * float(y);
        const float quad[12] = {x0, y0, 2.2f, x0, y0 + step, 2.2f,
            x0 + step, y0 + step, 2.2f, x0 + step, y0, 2.2f};
        positions.insert(positions.end(), quad, quad + 12);
      }
    }
    ANARIGeometry geometry = quads(d, positions);
    ANARIGroup group = groupOf(d, geometry, room.lampMaterial);
    ANARIInstance lamp = room.addLamp(0.f, 0.f, group);
    room.setWorld(room.lamps);
    room.setSamples(1);

    for (const char *sampling : {"none", "both"}) {
      room.setSampling(sampling);
      room.render();
      room.render();
      const auto frame = [&](const char *what) {
        const double start = seconds();
        room.render();
        std::printf("%6d triangles, %-4s  %-18s frame %7.1f ms, sync %6.2f ms, "
                    "%d objects written\n",
            2 * n * n, sampling, what, 1000.0 * (seconds() - start),
            floatProperty(d, "moonray.syncMillisecondsLastFrame"),
            intProperty(d, "moonray.objectsUpdatedLastSync"));
      };
      frame("nothing changed");
      room.setEmission(30.f, 30.f, 30.f);
      frame("emission edited");
      room.setEmission(20.f, 20.f, 20.f);
      frame("emission edited");
      setTranslation(d, lamp, 0.3f, 0.f);
      anariCommitParameters(d, lamp);
      frame("lamp moved");
      setTranslation(d, lamp, 0.f, 0.f);
      anariCommitParameters(d, lamp);
      frame("lamp moved");
      room.setWorld({});
      frame("lamp deleted");
      room.setWorld(room.lamps);
      frame("lamp added");
    }
    room.release({group, geometry});
  }
}

// --- anari.mesh_lights_churn -----------------------------------------------------

// One room after the other on the same device, each rendered with the
// samplings of `pattern`: rooms are separated by '|', samplings by ','.
void testChurn(ANARIDevice d, const std::string &pattern)
{
  size_t start = 0;
  int roomIndex = 0;
  float first = 0.f;
  while (start <= pattern.size()) {
    size_t end = pattern.find('|', start);
    if (end == std::string::npos)
      end = pattern.size();
    const std::string samplings = pattern.substr(start, end - start);
    Room room = buildRoom(d, 0.5f, 20.f, true);
    room.setSamples(64);
    size_t s0 = 0;
    while (s0 <= samplings.size()) {
      size_t s1 = samplings.find(',', s0);
      if (s1 == std::string::npos)
        s1 = samplings.size();
      const std::string sampling = samplings.substr(s0, s1 - s0);
      room.setSampling(sampling.c_str());
      const float mean = lowerPart(room.render()).luminance;
      std::printf("room %d, %s: %.4f, %d lights\n",
          roomIndex,
          sampling.c_str(),
          mean,
          intProperty(d, "moonray.meshLights"));
      if (first == 0.f)
        first = mean;
      CHECK_MSG(mean > 0.05f && within(mean, first, 0.1f),
          "every room is lit the same");
      s0 = s1 + 1;
    }
    room.release();
    ++roomIndex;
    start = end + 1;
  }
}

} // namespace

int main(int argc, char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "";

  // A crash inside the renderer names the functions it happened in, and what
  // was printed before it is not lost in a buffer.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_crash::install();

  ANARILibrary library = anariLoadLibrary("moonray", statusFunc, nullptr);
  if (!library) {
    std::printf("FAIL: could not load the moonray ANARI library\n");
    return 1;
  }
  ANARIDevice d = anariNewDevice(library, "default");
  if (!d) {
    std::printf("FAIL: could not create the device\n");
    return 1;
  }
  anariCommitParameters(d, d);

  if (mode == "noise")
    testNoise(d);
  else if (mode == "sides")
    testSides(d);
  else if (mode == "reflecting")
    testReflecting(d);
  else if (mode == "textured")
    testTextured(d);
  else if (mode == "rdla")
    testRdla(d);
  else if (mode == "spaces")
    testSpaces(d);
  else if (mode == "updates")
    testUpdates(d);
  else if (mode == "limit")
    testLimit(d);
  else if (mode == "visibility")
    testVisibility(d);
  else if (mode == "instances")
    testInstances(d);
  else if (mode == "editcost")
    measureEditCost(d);
  else if (mode == "churn")
    testChurn(d, argc > 2 ? argv[2] : "both,none|both|none,both|both");
  else {
    std::printf("usage: integration_mesh_lights <noise|sides|reflecting"
                "|textured|rdla|spaces|updates|limit|visibility|instances"
                "|churn>\n");
    return 2;
  }

  anariRelease(d, d);
  anariUnloadLibrary(library);

  if (failures == 0)
    std::printf("PASS %s\n", mode.c_str());
  else
    std::printf("%d failure(s) in %s\n", failures, mode.c_str());
  return failures == 0 ? 0 : 1;
}
