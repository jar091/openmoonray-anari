// Milestone 9 material tests: the physicallyBased mapping onto MoonRay's
// UsdPreviewSurface, and the image2D sampler.
//
// The assertions are derived from the model rather than from a screenshot:
//
//   * emission is added to the outgoing radiance directly, so a surface with
//     emissive E and no light renders exactly E;
//   * physicallyBased with metallic 0, roughness 1 and ior 1 *is* the matte
//     material - same Lambertian lobe, no Fresnel - so both must produce the
//     same image from the same scene;
//   * a texture whose texels are known colors must show those colors where
//     the UVs say they are.
//
// Usage: integration_materials <physically_based|image_texture>

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

bool close(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

const float kAlbedo[3] = {0.8f, 0.2f, 0.2f};
const float kBackground[4] = {0.f, 0.f, 1.f, 1.f};

// Swaps the scene's material for `material` and re-renders.
std::vector<float> renderWithMaterial(
    ANARIDevice d, test_scene::TriangleScene &scene, ANARIMaterial material)
{
  anariSetParameter(d, scene.surface, "material", ANARI_MATERIAL, &material);
  anariCommitParameters(d, scene.surface);
  return test_scene::renderToFloat(scene);
}

// --- anari.physically_based -------------------------------------------------

void testPhysicallyBased(ANARIDevice d)
{
  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // The matte reference: with irradiance pi the lit pixel is the albedo.
  auto matteImage = test_scene::renderToFloat(scene);
  const float *m = test_scene::centerPixel(matteImage, kSize);
  CHECK_MSG(close(m[0], kAlbedo[0], 0.05f), "the matte reference is wrong");

  // 1. physicallyBased in its Lambertian configuration must *be* that matte.
  ANARIMaterial lambert = anariNewMaterial(d, "physicallyBased");
  const float zero = 0.f;
  const float one = 1.f;
  anariSetParameter(d, lambert, "baseColor", ANARI_FLOAT32_VEC3, kAlbedo);
  anariSetParameter(d, lambert, "metallic", ANARI_FLOAT32, &zero);
  anariSetParameter(d, lambert, "roughness", ANARI_FLOAT32, &one);
  anariSetParameter(d, lambert, "ior", ANARI_FLOAT32, &one);
  anariCommitParameters(d, lambert);

  auto pbrImage = renderWithMaterial(d, scene, lambert);
  const float *p = test_scene::centerPixel(pbrImage, kSize);
  std::printf("matte %.4f %.4f %.4f vs physicallyBased %.4f %.4f %.4f\n",
      m[0], m[1], m[2], p[0], p[1], p[2]);
  for (int c = 0; c < 3; ++c) {
    CHECK_MSG(close(p[c], m[c], 0.02f),
        "physicallyBased with metallic 0, roughness 1, ior 1 must match matte");
  }

  // 2. A metal is a different material, not a tinted diffuse: with the same
  //    base color the image has to change measurably.
  ANARIMaterial metal = anariNewMaterial(d, "physicallyBased");
  const float rough = 0.3f;
  anariSetParameter(d, metal, "baseColor", ANARI_FLOAT32_VEC3, kAlbedo);
  anariSetParameter(d, metal, "metallic", ANARI_FLOAT32, &one);
  anariSetParameter(d, metal, "roughness", ANARI_FLOAT32, &rough);
  anariCommitParameters(d, metal);

  auto metalImage = renderWithMaterial(d, scene, metal);
  const float *mm = test_scene::centerPixel(metalImage, kSize);
  double diff = 0.0;
  bool finite = true;
  for (size_t i = 0; i < pbrImage.size(); ++i) {
    diff += std::fabs(double(metalImage[i]) - double(pbrImage[i]));
    if (!std::isfinite(metalImage[i]))
      finite = false;
  }
  std::printf("metal center %.4f %.4f %.4f, total |diff| vs diffuse %.1f\n",
      mm[0], mm[1], mm[2], diff);
  CHECK_MSG(finite, "the metallic render contains non-finite values");
  CHECK_MSG(diff > 1.0, "metallic 1 must not render the same as metallic 0");

  // 3. Emission is added to the outgoing radiance, so with the light switched
  //    off the surface renders exactly its emissive value.
  const float black[3] = {0.f, 0.f, 0.f};
  anariSetParameter(d, scene.light, "color", ANARI_FLOAT32_VEC3, black);
  anariCommitParameters(d, scene.light);

  const float emissive[3] = {0.3f, 0.4f, 0.5f};
  ANARIMaterial emitter = anariNewMaterial(d, "physicallyBased");
  anariSetParameter(d, emitter, "baseColor", ANARI_FLOAT32_VEC3, kAlbedo);
  anariSetParameter(d, emitter, "metallic", ANARI_FLOAT32, &zero);
  anariSetParameter(d, emitter, "roughness", ANARI_FLOAT32, &one);
  anariSetParameter(d, emitter, "ior", ANARI_FLOAT32, &one);
  anariSetParameter(d, emitter, "emissive", ANARI_FLOAT32_VEC3, emissive);
  anariCommitParameters(d, emitter);

  auto emissiveImage = renderWithMaterial(d, scene, emitter);
  const float *e = test_scene::centerPixel(emissiveImage, kSize);
  std::printf("emissive center %.4f %.4f %.4f (expected %.2f %.2f %.2f)\n",
      e[0], e[1], e[2], emissive[0], emissive[1], emissive[2]);
  for (int c = 0; c < 3; ++c) {
    CHECK_MSG(close(e[c], emissive[c], 0.02f),
        "an unlit emissive surface must render exactly its emissive radiance");
  }

  anariRelease(d, lambert);
  anariRelease(d, metal);
  anariRelease(d, emitter);
  test_scene::release(scene);
}

// --- anari.image_texture ----------------------------------------------------

void testImageTexture(ANARIDevice d)
{
  // A quad (two triangles) spanning x,y in [-1,1] with UVs in [0,1], so each
  // corner of the frame maps to a known texel of a 2x2 texture.
  const float vertices[4][3] = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const float uvs[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
  const uint32_t indices[2][3] = {{0, 1, 2}, {0, 2, 3}};

  // A 2x2 image, stored row-major from the lower left (ANARI's image origin),
  // so index 0 is the lower left, 1 the lower right, 2 the *upper left* and 3
  // the upper right.
  const float texels[4][3] = {
      {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 0.f}};

  auto scene =
      test_scene::build(d, kAlbedo, kBackground, ANARI_FLOAT32_VEC4, kSize);

  // Replace the triangle with the textured quad.
  ANARIGeometry quad = anariNewGeometry(d, "triangle");
  ANARIArray1D positionArray =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIArray1D uvArray =
      anariNewArray1D(d, uvs, nullptr, nullptr, ANARI_FLOAT32_VEC2, 4);
  ANARIArray1D indexArray =
      anariNewArray1D(d, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 2);
  anariSetParameter(d, quad, "vertex.position", ANARI_ARRAY1D, &positionArray);
  anariSetParameter(d, quad, "vertex.attribute0", ANARI_ARRAY1D, &uvArray);
  anariSetParameter(d, quad, "primitive.index", ANARI_ARRAY1D, &indexArray);
  anariCommitParameters(d, quad);
  anariRelease(d, positionArray);
  anariRelease(d, uvArray);
  anariRelease(d, indexArray);

  anariSetParameter(d, scene.surface, "geometry", ANARI_GEOMETRY, &quad);
  anariCommitParameters(d, scene.surface);

  // The sampler: a 2x2 image. MoonRay filters textures through OpenImageIO,
  // so the probes below sit at the texel *centers*, where a bilinear lookup
  // returns the texel itself.
  ANARIArray2D image =
      anariNewArray2D(d, texels, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2, 2);
  ANARISampler sampler = anariNewSampler(d, "image2D");
  anariSetParameter(d, sampler, "image", ANARI_ARRAY2D, &image);
  anariSetParameter(d, sampler, "inAttribute", ANARI_STRING, "attribute0");
  anariCommitParameters(d, sampler);
  anariRelease(d, image);

  ANARIMaterial textured = anariNewMaterial(d, "matte");
  anariSetParameter(d, textured, "color", ANARI_SAMPLER, &sampler);
  anariCommitParameters(d, textured);

  auto pixels = renderWithMaterial(d, scene, textured);
  CHECK_MSG(!pixels.empty(), "the textured render produced no image");

  if (!pixels.empty()) {
    // Where to probe is arithmetic: the quad fills x,y in [-1,1] and, with
    // the camera 3 units back and a 60 degree vertical field of view, the
    // frame spans +-3*tan(30) = +-1.732. So the quad occupies the frame
    // fraction 0.2113 .. 0.7887, and UV u maps to 0.2113 + u*0.5774. The
    // texel centers (u = 0.25 and 0.75) are therefore at 0.356 and 0.644.
    auto at = [&](float fx, float fy) {
      const auto x = uint32_t(fx * kSize);
      const auto y = uint32_t(fy * kSize);
      return pixels.data() + 4 * (size_t(y) * kSize + x);
    };

    struct Probe
    {
      const char *name;
      float fx, fy;
      const float *expected;
    };
    const Probe probes[] = {{"lower left", 0.356f, 0.356f, texels[0]},
        {"lower right", 0.644f, 0.356f, texels[1]},
        {"upper left", 0.356f, 0.644f, texels[2]},
        {"upper right", 0.644f, 0.644f, texels[3]}};

    for (const auto &probe : probes) {
      const float *c = at(probe.fx, probe.fy);
      std::printf("%-12s %.3f %.3f %.3f (expected %.1f %.1f %.1f)\n",
          probe.name, c[0], c[1], c[2],
          probe.expected[0], probe.expected[1], probe.expected[2]);
      for (int ch = 0; ch < 3; ++ch) {
        // The tolerance covers the texture filter: the 16 samples per pixel
        // land on slightly different UVs, so even at a texel center the
        // result carries a little of the neighbours.
        CHECK_MSG(close(c[ch], probe.expected[ch], 0.1f),
            "a texel does not show where its UVs say it should");
      }
    }
  }

  anariRelease(d, sampler);
  anariRelease(d, textured);
  anariRelease(d, quad);
  test_scene::release(scene);
}

} // namespace

int main(int argc, const char **argv)
{
  const std::string mode = argc > 1 ? argv[1] : "physically_based";

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

  if (mode == "physically_based")
    testPhysicallyBased(device);
  else if (mode == "image_texture")
    testImageTexture(device);
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
