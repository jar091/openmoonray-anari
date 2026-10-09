// Milestone 1 native smoke render: in-process MoonRay CPU render of a single
// triangle with a real camera, material and light — no ANARI involved.
//
// The embedding sequence follows docs/adr/0002-native-moonray-embedding.md,
// which was verified against the pinned moonray CLI
// (cmd/raas_cmd/moonray/moonray.cc). Validation is metric-based (finite
// values, nonzero coverage), never a golden-image byte compare (TESTING.md).
//
// Required environment: RDL2_DSO_PATH must point at the installed rdl2dso
// directory (set by the CTest wiring in CMakeLists.txt).

#include <moonray/rendering/rndr/RenderContext.h>

#include <scene_rdl2/common/fb_util/FbTypes.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

#define CHECK_MSG(cond, msg)                                                            \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, msg);       \
            ++failures;                                                                 \
        }                                                                               \
    } while (0)

#define CHECK(cond) CHECK_MSG(cond, "")

constexpr int kWidth = 64;
constexpr int kHeight = 64;

} // namespace

int main()
{
    using namespace scene_rdl2::rdl2;

    const char *dsoPath = std::getenv("RDL2_DSO_PATH");
    if (!dsoPath || !*dsoPath) {
        std::printf("FAIL: RDL2_DSO_PATH is not set — cannot locate MoonRay DSOs\n");
        return 1;
    }

    moonray::rndr::RenderOptions options;
    options.setDsoPath(dsoPath);
    // Single-threaded is enough for 64x64 and keeps the smoke test cheap.
    options.setThreads(4);

    std::stringstream initMessages;
    moonray::rndr::initGlobalDriver(options);

    {
        moonray::rndr::RenderContext ctx(options, &initMessages);
        SceneContext &sc = ctx.getSceneContext();

        // --- camera ---------------------------------------------------------
        SceneObject *cameraObj = sc.createSceneObject("PerspectiveCamera", "/camera");
        Camera *camera = cameraObj->asA<Camera>();
        CHECK(camera != nullptr);

        // --- geometry: one large triangle in front of the camera ------------
        SceneObject *meshObj = sc.createSceneObject("RdlMeshGeometry", "/triangle");
        Geometry *mesh = meshObj->asA<Geometry>();
        CHECK(mesh != nullptr);
        {
            SceneObject::UpdateGuard guard(meshObj);
            meshObj->set("face_vertex_count", IntVector{3});
            meshObj->set("vertices_by_index", IntVector{0, 1, 2});
            meshObj->set("vertex_list_0",
                         Vec3fVector{{-3.0f, -3.0f, -3.0f},
                                     {3.0f, -3.0f, -3.0f},
                                     {0.0f, 3.0f, -3.0f}});
        }

        // --- material -------------------------------------------------------
        SceneObject *materialObj = sc.createSceneObject("UsdPreviewSurface", "/material");
        Material *material = materialObj->asA<Material>();
        CHECK(material != nullptr);

        // --- light: uniform environment light lights everything -------------
        SceneObject *lightObj = sc.createSceneObject("EnvLight", "/light");
        Light *light = lightObj->asA<Light>();
        CHECK(light != nullptr);

        SceneObject *lightSetObj = sc.createSceneObject("LightSet", "/lightSet");
        LightSet *lightSet = lightSetObj->asA<LightSet>();
        CHECK(lightSet != nullptr);
        {
            SceneObject::UpdateGuard guard(lightSetObj);
            lightSet->add(light);
        }

        // --- geometry set (mandatory: the BVH is built from geometry sets) --
        SceneObject *geoSetObj = sc.createSceneObject("GeometrySet", "/geometrySet");
        GeometrySet *geometrySet = geoSetObj->asA<GeometrySet>();
        CHECK(geometrySet != nullptr);
        {
            SceneObject::UpdateGuard guard(geoSetObj);
            geometrySet->add(mesh);
        }

        // --- layer ----------------------------------------------------------
        SceneObject *layerObj = sc.createSceneObject("Layer", "/layer");
        Layer *layer = layerObj->asA<Layer>();
        CHECK(layer != nullptr);
        {
            SceneObject::UpdateGuard guard(layerObj);
            layer->assign(mesh, "", material, lightSet);
        }

        // --- scene variables -------------------------------------------------
        SceneVariables &vars = sc.getSceneVariables();
        {
            SceneObject::UpdateGuard guard(&vars);
            vars.set(SceneVariables::sImageWidth, kWidth);
            vars.set(SceneVariables::sImageHeight, kHeight);
            // 2^2 = 4 samples/pixel — enough for coverage, cheap to run.
            vars.set(SceneVariables::sPixelSamplesSqrt, 2);
            // uniform sampling: BATCH must not silently remap to PROGRESSIVE
            // (ADR 0002); 0 == SamplingMode::UNIFORM.
            vars.set(SceneVariables::sSamplingMode, 0);
            vars.set(SceneVariables::sCamera, cameraObj);
            vars.set(SceneVariables::sLayer, layerObj);
        }

        ctx.initialize(initMessages);
        ctx.setRenderMode(moonray::rndr::RenderMode::BATCH);

        ctx.startFrame();
        while (!ctx.isFrameComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ctx.stopFrame();

        // --- validate the framebuffer ---------------------------------------
        scene_rdl2::fb_util::RenderBuffer buffer;
        ctx.snapshotRenderBuffer(&buffer, /*untile=*/true, /*parallel=*/true,
                                 /*usePrimaryAov=*/true);

        CHECK_MSG(buffer.getWidth() == kWidth, "unexpected buffer width");
        CHECK_MSG(buffer.getHeight() == kHeight, "unexpected buffer height");

        const scene_rdl2::fb_util::RenderColor *pixels = buffer.getData();
        const unsigned pixelCount = buffer.getWidth() * buffer.getHeight();

        unsigned litPixels = 0;
        bool allFinite = true;
        for (unsigned i = 0; i < pixelCount; ++i) {
            const auto &p = pixels[i];
            for (int c = 0; c < 4; ++c) {
                if (!std::isfinite(p[c])) {
                    allFinite = false;
                }
            }
            if (p[0] + p[1] + p[2] > 0.0f) {
                ++litPixels;
            }
        }

        CHECK_MSG(allFinite, "framebuffer contains non-finite values");
        // The triangle spans most of the 64x64 frame; require at least 10%
        // coverage so a black frame or a missing BVH cannot pass.
        CHECK_MSG(litPixels >= pixelCount / 10, "framebuffer is (nearly) black");

        std::printf("rendered %ux%u, %u/%u lit pixels\n",
                    buffer.getWidth(), buffer.getHeight(), litPixels, pixelCount);
    } // ~RenderContext before cleanUpGlobalDriver (ADR 0002)

    moonray::rndr::cleanUpGlobalDriver();

    if (failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
}
